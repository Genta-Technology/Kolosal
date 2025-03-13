#pragma once

#include <string>
#include <json.hpp>
#include <filesystem>
#include <map>
#include <atomic>

using json = nlohmann::json;

namespace Model
{
    // ModelVariant structure remains mostly the same
    struct ModelVariant {
        std::string type;
        std::string path;
        std::string downloadLink;
        bool isDownloaded;
        double downloadProgress;
        int lastSelected;
        std::atomic_bool cancelDownload{ false };

        // Default constructor is fine.
        ModelVariant() = default;

        // Custom copy constructor.
        ModelVariant(const ModelVariant& other)
            : type(other.type)
            , path(other.path)
            , downloadLink(other.downloadLink)
            , isDownloaded(other.isDownloaded)
            , downloadProgress(other.downloadProgress)
            , lastSelected(other.lastSelected)
            , cancelDownload(false) // Always initialize to false on copy.
        {
        }

        // Custom copy assignment operator.
        ModelVariant& operator=(const ModelVariant& other) {
            if (this != &other) {
                type = other.type;
                path = other.path;
                downloadLink = other.downloadLink;
                isDownloaded = other.isDownloaded;
                downloadProgress = other.downloadProgress;
                lastSelected = other.lastSelected;
                cancelDownload = false; // Reinitialize the cancellation flag.
            }
            return *this;
        }
    };

    inline void to_json(nlohmann::json& j, const ModelVariant& v)
    {
        j = nlohmann::json{
            {"type", v.type},
            {"path", v.path},
            {"downloadLink", v.downloadLink},
            {"isDownloaded", v.isDownloaded},
            {"downloadProgress", v.downloadProgress},
            {"lastSelected", v.lastSelected} };
    }

    inline void from_json(const nlohmann::json& j, ModelVariant& v)
    {
        j.at("type").get_to(v.type);
        j.at("path").get_to(v.path);
        j.at("downloadLink").get_to(v.downloadLink);
        j.at("isDownloaded").get_to(v.isDownloaded);
        j.at("downloadProgress").get_to(v.downloadProgress);
        j.at("lastSelected").get_to(v.lastSelected);
    }

    // Refactored ModelData to use a map of variants
    struct ModelData
    {
        std::string name;
        std::string author;
        std::map<std::string, ModelVariant> variants;

        // Constructor with no variants
        ModelData(const std::string& name = "", const std::string& author = "")
            : name(name), author(author) {
        }

        // Add a variant to the model
        void addVariant(const std::string& variantType, const ModelVariant& variant) {
            variants[variantType] = variant;
        }

        // Check if a variant exists
        bool hasVariant(const std::string& variantType) const {
            return variants.find(variantType) != variants.end();
        }

        // Get a variant (const version)
        const ModelVariant* getVariant(const std::string& variantType) const {
            auto it = variants.find(variantType);
            return (it != variants.end()) ? &(it->second) : nullptr;
        }

        // Get a variant (non-const version)
        ModelVariant* getVariant(const std::string& variantType) {
            auto it = variants.find(variantType);
            return (it != variants.end()) ? &(it->second) : nullptr;
        }
    };

    inline void to_json(nlohmann::json& j, const ModelData& m)
    {
        j = nlohmann::json{
            {"name", m.name},
            {"author", m.author},
            {"variants", m.variants}
        };
    }

    inline void from_json(const nlohmann::json& j, ModelData& m)
    {
        j.at("name").get_to(m.name);
        j.at("author").get_to(m.author);
        j.at("variants").get_to(m.variants);
    }
} // namespace Model