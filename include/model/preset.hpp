#pragma once

#include <string>
#include <json.hpp>

using json = nlohmann::json;

namespace Model
{
    struct ModelPreset
    {
        int id;
        int lastModified;
        std::string name;
        std::string systemPrompt;
        float temperature;
        float top_p;

        // TODO: Use int instead of float
        // I use float right now because ImGui::SliderFloat requires a float
        // so it needed to create a new custom slider for int
        float top_k;
        int random_seed;

		// TODO: Use int instead of float
        float min_length;

        // generation
        // TODO: Use int instead of float
        float max_new_tokens;

        ModelPreset(
            int id = 0,
            int lastModified = 0,
            const std::string& name = "",
            const std::string& systemPrompt = "",
            float temperature = 0.7f,
            float top_p = 0.9f,
            float top_k = 50.0f,
            int random_seed = 42,
            float min_length = 0.0f,
            float max_new_tokens = 0.0f)
            : id(id)
            , lastModified(lastModified)
            , name(name)
            , systemPrompt(systemPrompt)
            , temperature(temperature)
            , top_p(top_p)
            , top_k(top_k)
            , random_seed(random_seed)
            , min_length(min_length)
            , max_new_tokens(max_new_tokens) {}

        bool operator==(const ModelPreset& other) const
        {
            return id == other.id &&
                name == other.name &&
                systemPrompt == other.systemPrompt &&
                temperature == other.temperature &&
                top_p == other.top_p &&
                top_k == other.top_k &&
                random_seed == other.random_seed &&
                min_length == other.min_length &&
                max_new_tokens == other.max_new_tokens;
        }

        bool operator!=(const ModelPreset& other) const
        {
            return !(*this == other);
        }
    };

    inline void to_json(json& j, const ModelPreset& p)
    {
        j = json{
            {"id", p.id},
            {"lastModified", p.lastModified},
            {"name", p.name},
            {"systemPrompt", p.systemPrompt},
            {"temperature", p.temperature},
            {"top_p", p.top_p},
            {"top_k", p.top_k},
            {"random_seed", p.random_seed},
            {"min_length", p.min_length},
            {"max_new_tokens", p.max_new_tokens} };
    }

    inline void from_json(const json& j, ModelPreset& p)
    {
        j.at("id").get_to(p.id);
        j.at("lastModified").get_to(p.lastModified);
        j.at("name").get_to(p.name);
        j.at("systemPrompt").get_to(p.systemPrompt);
        j.at("temperature").get_to(p.temperature);
        j.at("top_p").get_to(p.top_p);
        j.at("top_k").get_to(p.top_k);
        j.at("random_seed").get_to(p.random_seed);
        j.at("min_length").get_to(p.min_length);
        j.at("max_new_tokens").get_to(p.max_new_tokens);
    }
} // namespace Model