#pragma once

#include "common.hpp"

#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>
#include <sstream>

// nlohmann/json library
#include "json.hpp"

using json = nlohmann::json;

namespace Chat
{
    struct Message
    {
        int id;
        bool isLiked;
        bool isDisliked;
        std::string role;
        std::string content;
        std::string modelName;
        float tps;
        std::chrono::system_clock::time_point timestamp;

        Message(
            int id = 0,
            const std::string& role = "user",
            const std::string& content = "",
			const std::string& modelName = "",
			const float tps = 0.0f,
            bool isLiked = false,
            bool isDisliked = false,
            const std::chrono::system_clock::time_point& timestamp = std::chrono::system_clock::now())
            : id(id)
            , isLiked(isLiked)
            , isDisliked(isDisliked)
            , role((role == "user" || role == "assistant") // Check if the role is either user or assistant
                ? role
                : throw std::invalid_argument("Invalid role: " + role))
            , content(content)
			, tps(tps)
            , timestamp(timestamp)
            , modelName(modelName){
        }
    };

    inline void to_json(json& j, const Message& msg)
    {
        j = json{
            {"id", msg.id},
            {"isLiked", msg.isLiked},
            {"isDisliked", msg.isDisliked},
            {"role", msg.role},
            {"content", msg.content},
            {"timestamp", timePointToString(msg.timestamp)},
			{"tps", msg.tps},
			{"modelName", msg.modelName}
        };
    }

    inline void from_json(const json& j, Message& msg)
    {
        msg.id          = j.at("id").get<int>();
        msg.isLiked     = j.at("isLiked").get<bool>();
        msg.isDisliked  = j.at("isDisliked").get<bool>();
        msg.role        = j.at("role").get<std::string>();
        msg.content     = j.at("content").get<std::string>();
        msg.timestamp   = stringToTimePoint(j.at("timestamp").get<std::string>());
        msg.tps         = j.value("tps", 0.0f);
        msg.modelName   = j.value("modelName", "");
    }

    struct ChatHistory
    {
        int id;
        int lastModified;
        std::string name;
        std::vector<Message> messages;

        ChatHistory(
            const int id = 0,
            const int lastModified = 0,
            const std::string& name = "untitled",
            const std::vector<Message>& messages = {})
            : id(id)
            , lastModified(lastModified)
            , name(name)
            , messages(messages) {
        }
    };

    inline void to_json(json& j, const ChatHistory& chatHistory)
    {
        j = json{
            {"id", chatHistory.id},
            {"lastModified", chatHistory.lastModified},
            {"name", chatHistory.name},
            {"messages", chatHistory.messages} };
    }

    inline void from_json(const json& j, ChatHistory& chatHistory)
    {
        j.at("id").get_to(chatHistory.id);
        j.at("lastModified").get_to(chatHistory.lastModified);
        j.at("name").get_to(chatHistory.name);
        j.at("messages").get_to(chatHistory.messages);
    }

} // namespace Chat