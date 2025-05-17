#pragma once

#include "model.hpp"
#include "logger.hpp"

#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <future>
#include <algorithm>
#include <curl/curl.h>

namespace Model
{
    class IModelPersistence
    {
    public:
        virtual ~IModelPersistence() = default;
        virtual std::future<std::vector<ModelData>> loadAllModels() = 0;
        virtual std::future<void> downloadModelVariant(ModelData& modelData, const std::string& variantType) = 0;
        virtual std::future<void> saveModelData(const ModelData& modelData) = 0;
        virtual std::future<void> deleteModelVariant(ModelData& modelData, const std::string& variantType) = 0;
    };

    class FileModelPersistence : public IModelPersistence
    {
    public:
        explicit FileModelPersistence(const std::string& basePath)
            : m_basePath(basePath)
        {
            if (!std::filesystem::exists(m_basePath))
            {
                std::filesystem::create_directories(m_basePath);
            }
        }

        std::future<std::vector<ModelData>> loadAllModels() override
        {
            return std::async(std::launch::async, [this]() -> std::vector<ModelData> {
                std::vector<ModelData> models;
                try
                {
                    for (const auto& entry : std::filesystem::directory_iterator(m_basePath))
                    {
                        if (entry.path().extension() == ".json")
                        {
                            std::ifstream file(entry.path());
                            if (file.is_open())
                            {
                                nlohmann::json j;
                                file >> j;
                                models.push_back(j.get<ModelData>());
                            }
                        }
                    }
                }
                catch (...)
                {
                    // Return whatever was read successfully.
                }
                return models; });
        }

        std::future<void> downloadModelVariant(ModelData& modelData, const std::string& variantType) override
        {
            return std::async(std::launch::async, [this, &modelData, variantType]() {
                // Check if variant exists
                auto variantIter = modelData.variants.find(variantType);
                if (variantIter == modelData.variants.end()) {
                    LOG_ERROR("[FileModelPersistence::downloadModelVariant] Variant '{}' not found in model '{}'", variantType, modelData.name);
                    return;
                }

                ModelVariant& variant = variantIter->second;

                // Reset cancellation flag at the start.
                variant.cancelDownload = false;

                CURL* curl = curl_easy_init();
                if (curl)
                {
                    // Ensure the target directory exists.
                    std::filesystem::create_directories(std::filesystem::path(variant.path).parent_path());
                    std::ofstream file(variant.path, std::ios::binary);
                    if (!file.is_open())
                    {
                        LOG_ERROR("[FileModelPersistence::downloadModelVariant] Failed to open file for writing: {}", variant.path);
                        curl_easy_cleanup(curl);
                        return;
                    }

                    curl_easy_setopt(curl, CURLOPT_URL, variant.downloadLink.c_str());
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
                    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &variant);
                    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

                    CURLcode res = curl_easy_perform(curl);
                    if (res != CURLE_OK)
                    {
                        if (res == CURLE_ABORTED_BY_CALLBACK) {
                            // Download was canceled:
                            file.close();
                            std::filesystem::remove(variant.path);  // Remove incomplete file
                            variant.downloadProgress = 0.0;
                            variant.isDownloaded = false;
                            LOG_WARNING("[FileModelPersistence::downloadModelVariant] Download of '{}' for model '{}' was canceled and incomplete file removed.", variantType, modelData.name);
                        }
                        else {
                            LOG_ERROR("[FileModelPersistence::downloadModelVariant] CURL error while downloading '{}': {}", variantType, curl_easy_strerror(res));
                            // Handle other error cases as needed.
                        }
                    }
                    else
                    {
                        variant.isDownloaded = true;
                        variant.downloadProgress = 100.0;
                        LOG_INFO("[FileModelPersistence::downloadModelVariant] Successfully downloaded '{}' for model '{}'.", variantType, modelData.name);
                        // Save the updated model data.
                        saveModelData(modelData).get();
                    }
                    curl_easy_cleanup(curl);
                    file.close();
                }
                });
        }

        std::future<void> saveModelData(const ModelData& modelData) override
        {
            return std::async(std::launch::async, [this, modelData]() {
                std::string modelDataFilename = modelData.name;
                std::replace(modelDataFilename.begin(), modelDataFilename.end(), ' ', '-');
                std::transform(modelDataFilename.begin(), modelDataFilename.end(), modelDataFilename.begin(), ::tolower);
                std::ofstream file(m_basePath + "/" + modelDataFilename + ".json");
                if (file.is_open())
                {
                    nlohmann::json j = modelData;
                    file << j.dump(4);
                    file.close();
                }
                });
        }

        std::future<void> deleteModelVariant(ModelData& modelData, const std::string& variantType) override
        {
            return std::async(std::launch::async, [this, &modelData, variantType]() {
                // Check if variant exists
                auto variantIter = modelData.variants.find(variantType);
                if (variantIter == modelData.variants.end()) {
                    LOG_ERROR("[FileModelPersistence::deleteModelVariant] Variant '{}' not found in model '{}'", variantType, modelData.name);
                    return;
                }

                ModelVariant& variant = variantIter->second;

                // Check if the file exists and attempt to remove it.
                if (std::filesystem::exists(variant.path))
                {
                    try {
                        std::filesystem::remove(variant.path);
                        LOG_INFO("[FileModelPersistence::deleteModelVariant] Deleted file '{}'", variant.path);
                    }
                    catch (const std::filesystem::filesystem_error& e) {
                        LOG_ERROR("[FileModelPersistence::deleteModelVariant] Error deleting file '{}': {}", variant.path, e.what());
                    }
                }
                // Reset the variant's state so that it can be redownloaded.
                variant.isDownloaded = false;
                variant.downloadProgress = 0.0;
                variant.lastSelected = 0;

                // Save the updated model data.
                saveModelData(modelData).get();
                });
        }

    private:
        static size_t write_data(void* ptr, size_t size, size_t nmemb, void* userdata)
        {
            std::ofstream* stream = static_cast<std::ofstream*>(userdata);
            size_t written = 0;
            if (stream->is_open())
            {
                stream->write(static_cast<const char*>(ptr), size * nmemb);
                written = size * nmemb;
            }
            return written;
        }

        static int progress_callback(void* ptr, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
        {
            ModelVariant* variant = static_cast<ModelVariant*>(ptr);
            if (total > 0) {
                variant->downloadProgress = static_cast<double>(now) / static_cast<double>(total) * 100.0;
            }
            // If cancel flag is set, abort the transfer.
            if (variant->cancelDownload)
            {
                LOG_WARNING("[FileModelPersistence::progress_callback] Download canceled for variant '{}'", variant->path);
                return 1; // non-zero return value signals curl to abort
            }
            return 0;
        }

        std::string m_basePath;
    };
} // namespace Model