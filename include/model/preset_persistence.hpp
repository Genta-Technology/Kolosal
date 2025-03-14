#pragma once

#include "preset.hpp"

#include <vector>
#include <future>
#include <filesystem>
#include <fstream>
#include <shared_mutex>

namespace Model
{
    class IPresetPersistence
    {
    public:
        virtual ~IPresetPersistence() = default;
        virtual std::future<bool> savePreset(const ModelPreset& preset) = 0;
        virtual std::future<bool> savePresetToPath(const ModelPreset& preset, const std::filesystem::path& filePath) = 0;
        virtual std::future<bool> deletePreset(const std::string& presetName) = 0;
        virtual std::future<std::vector<ModelPreset>> loadAllPresets() = 0;
    };

    class FilePresetPersistence : public IPresetPersistence
    {
    public:
        explicit FilePresetPersistence(const std::string& basePath)
            : m_basePath(basePath)
        {
            // Ensure base path exists
            if (!std::filesystem::exists(m_basePath))
            {
                std::filesystem::create_directories(m_basePath);
            }
        }

        std::future<bool> savePreset(const ModelPreset& preset) override
        {
            return std::async(std::launch::async, [this, preset]()
                { return savePresetInternal(preset); });
        }

        std::future<bool> savePresetToPath(const ModelPreset& preset, const std::filesystem::path& filePath) override
        {
            return std::async(std::launch::async, [this, preset, filePath]()
                { return savePresetToPathInternal(preset, filePath); });
        }

        std::future<bool> deletePreset(const std::string& presetName) override
        {
            return std::async(std::launch::async, [this, presetName]()
                { return deletePresetInternal(presetName); });
        }

        std::future<std::vector<ModelPreset>> loadAllPresets() override
        {
            return std::async(std::launch::async, [this]()
                { return loadAllPresetsInternal(); });
        }

    private:
        const std::string m_basePath;
        mutable std::shared_mutex m_ioMutex;

        bool savePresetInternal(const ModelPreset& preset)
        {
            std::unique_lock<std::shared_mutex> lock(m_ioMutex);
            try
            {
                std::filesystem::path filePath = getPresetPath(preset.name);

                // Open file before JSON serialization to fail early if file can't be opened
                std::ofstream file(filePath);
                if (!file.is_open())
                {
					std::cerr << "[PRESET PERSISTENCE] [ERROR] Failed to open file for writing: " << filePath.string() << std::endl;
                    return false;
                }

                // Serialize to JSON with better exception handling
                nlohmann::json j;
                try {
                    j = preset;
                }
                catch (const std::exception& e) {
					std::cerr << "[PRESET PERSISTENCE] [ERROR] JSON serialization failed: " << e.what() << std::endl;
                    return false;
                }

                // Write to file with exception handling
                try {
                    file << j.dump(4);
                }
                catch (const std::exception& e) {
					std::cerr << "[PRESET PERSISTENCE] [ERROR] Failed to write JSON to file: " << e.what() << std::endl;
                    return false;
                }
                return true;
            }
            catch (const std::exception& e)
            {
				std::cerr << "[PRESET PERSISTENCE] [ERROR] Failed to save preset: " << e.what() << std::endl;
                return false;
            }
        }

        bool savePresetToPathInternal(const ModelPreset& preset, const std::filesystem::path& filePath)
        {
            std::unique_lock<std::shared_mutex> lock(m_ioMutex);
            try
            {
                nlohmann::json j = preset;
                std::ofstream file(filePath);
                if (!file.is_open())
                {
                    return false;
                }
                file << j.dump(4);
                return true;
            }
            catch (const std::exception&)
            {
                // Log error
                return false;
            }
        }

        bool deletePresetInternal(const std::string& presetName)
        {
            std::unique_lock<std::shared_mutex> lock(m_ioMutex);
            try
            {
                std::filesystem::path filePath = getPresetPath(presetName);
                if (std::filesystem::exists(filePath))
                {
                    std::filesystem::remove(filePath);
                }
                return true;
            }
            catch (const std::exception&)
            {
                // Log error
                return false;
            }
        }

        std::vector<ModelPreset> loadAllPresetsInternal()
        {
            std::shared_lock<std::shared_mutex> lock(m_ioMutex);
            std::vector<ModelPreset> presets;
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
                            ModelPreset preset = j.get<ModelPreset>();
                            presets.push_back(preset);
                        }
                    }
                }
            }
            catch (const std::exception&)
            {
                // Log error
            }
            return presets;
        }

        std::filesystem::path getPresetPath(const std::string& presetName) const
        {
            return std::filesystem::path(m_basePath) / (presetName + ".json");
        }
    };
} // namespace Model