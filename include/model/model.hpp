#pragma once

#include <string>
#include <json.hpp>
#include <filesystem>
#include <map>
#include <atomic>

using json = nlohmann::json;

namespace Model
{
    struct ModelVariant {
        std::string type;
        std::string path;
        std::string downloadLink;
        bool isDownloaded;
        double downloadProgress;
        int lastSelected;
        std::atomic_bool cancelDownload{ false };
        float size;

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
            , cancelDownload(false)
			, size(other.size)
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
                cancelDownload = false;
				size = other.size;
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
            {"lastSelected", v.lastSelected},
            {"size", v.size} };
    }

    inline void from_json(const nlohmann::json& j, ModelVariant& v)
    {
        j.at("type").get_to(v.type);
        j.at("path").get_to(v.path);
        j.at("downloadLink").get_to(v.downloadLink);
        j.at("isDownloaded").get_to(v.isDownloaded);
        j.at("downloadProgress").get_to(v.downloadProgress);
        j.at("lastSelected").get_to(v.lastSelected);
		j.at("size").get_to(v.size);
    }

    // Refactored ModelData to use a map of variants
    struct ModelData
    {
        std::string name;
        std::string author;
        std::map<std::string, ModelVariant> variants;
        float_t hidden_size;
        float_t attention_heads;
        float_t hidden_layers;
        float_t kv_heads;

        // Constructor with no variants
        ModelData(const std::string& name = "", const std::string& author = "",
            const float_t hidden_size = 0, const float_t attention_heads = 0,
            const float_t hidden_layers = 0, const float_t kv_heads = 0)
            : name(name), author(author), hidden_size(hidden_size), attention_heads(attention_heads)
            , hidden_layers(hidden_layers), kv_heads(kv_heads) {
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
            {"variants", m.variants},
			{"hidden_size", m.hidden_size},
			{"attention_heads", m.attention_heads},
			{"hidden_layers", m.hidden_layers},
			{"kv_heads", m.kv_heads}
        };
    }

    inline void from_json(const nlohmann::json& j, ModelData& m)
    {
        j.at("name").get_to(m.name);
        j.at("author").get_to(m.author);
        j.at("variants").get_to(m.variants);
		j.at("hidden_size").get_to(m.hidden_size);
		j.at("attention_heads").get_to(m.attention_heads);
		j.at("hidden_layers").get_to(m.hidden_layers);
		j.at("kv_heads").get_to(m.kv_heads);
    }
} // namespace Model