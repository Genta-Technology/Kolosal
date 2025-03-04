#pragma once

#include <string>
#include <json.hpp>
#include <filesystem>

using json = nlohmann::json;

namespace Model
{
    // In model.hpp or the appropriate header:
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

    inline void to_json(nlohmann::json &j, const ModelVariant &v)
    {
        j = nlohmann::json{
            {"type", v.type},
            {"path", v.path},
            {"downloadLink", v.downloadLink},
            {"isDownloaded", v.isDownloaded},
            {"downloadProgress", v.downloadProgress},
            {"lastSelected", v.lastSelected}};
    }

    inline void from_json(const nlohmann::json &j, ModelVariant &v)
    {
        j.at("type").get_to(v.type);
        j.at("path").get_to(v.path);
        j.at("downloadLink").get_to(v.downloadLink);
        j.at("isDownloaded").get_to(v.isDownloaded);
        j.at("downloadProgress").get_to(v.downloadProgress);
        j.at("lastSelected").get_to(v.lastSelected);
    }

    struct ModelData
    {
        std::string name;
        std::string author;
        ModelVariant fullPrecision;
		ModelVariant quantized8Bit;
        ModelVariant quantized4Bit;

        ModelData(const std::string &name = "",
			      const std::string& author = "",
                  const ModelVariant &fullPrecision = ModelVariant(),
                  const ModelVariant &quantized8Bit = ModelVariant(),
                  const ModelVariant &quantized4Bit = ModelVariant())
            : name(name)
			, author(author)
            , fullPrecision(fullPrecision)
			, quantized8Bit(quantized8Bit)
            , quantized4Bit(quantized4Bit) {}
    };

    inline void to_json(nlohmann::json &j, const ModelData &m)
    {
        j = nlohmann::json{
            {"name", m.name},
			{"author", m.author},
            {"fullPrecision", m.fullPrecision},
			{"quantized8Bit", m.quantized8Bit},
            {"quantized4Bit", m.quantized4Bit}};
    }

    inline void from_json(const nlohmann::json &j, ModelData &m)
    {
        j.at("name").get_to(m.name);
		j.at("author").get_to(m.author);
        j.at("fullPrecision").get_to(m.fullPrecision);
		j.at("quantized8Bit").get_to(m.quantized8Bit);
        j.at("quantized4Bit").get_to(m.quantized4Bit);
    }
} // namespace Model