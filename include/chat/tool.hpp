#pragma once

#include <string>
#include <map>
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

namespace Chat
{
    struct ToolCall {
        std::string func_name;
        std::map<std::string, std::string> params;
        size_t start_index;
        size_t end_index;
        std::string output = "";

        // Assignment operator
        ToolCall& operator=(const ToolCall& other) {
            if (this != &other) {
                func_name   = other.func_name;
                params      = other.params;
                start_index = other.start_index;
                end_index   = other.end_index;
                output      = other.output;
            }
            return *this;
        }
    };

    // Add serialization for ToolCall
    inline void to_json(json& j, const ToolCall& toolCall)
    {
        j = json{
            {"func_name",   toolCall.func_name},
            {"params",      toolCall.params},
            {"start_index", toolCall.start_index},
            {"end_index",   toolCall.end_index},
            {"output",      toolCall.output}
        };
    }

    // Add deserialization for ToolCall
    inline void from_json(const json& j, ToolCall& toolCall)
    {
        j.at("func_name").get_to(toolCall.func_name);
        j.at("params").get_to(toolCall.params);
        j.at("start_index").get_to(toolCall.start_index);
        j.at("end_index").get_to(toolCall.end_index);
        j.at("output").get_to(toolCall.output);
    }

	struct ToolResult {
        ToolCall toolCall;
        std::string result;
        bool success;
        std::string error;

		// Assignment operator
		ToolResult& operator=(const ToolResult& other) {
			if (this != &other) {
				toolCall = other.toolCall;
				result = other.result;
				success = other.success;
				error = other.error;
			}
			return *this;
		}
	};
}