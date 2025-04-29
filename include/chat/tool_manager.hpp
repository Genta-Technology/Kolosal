#pragma once

#include "tool.hpp"
#include "mcp_sse_client.h"
#include "mcp_stdio_client.h"

#include <string>
#include <vector>
#include <map>
#include <regex>
#include <iostream>
#include <thread>
#include <mutex>
#include <future>
#include <memory>

namespace Chat
{
    enum class ClientType {
        SSE,
        STDIO
    };

    class ToolManager {
    public:
        // Singleton pattern
        static ToolManager& getInstance() {
            static ToolManager instance;
            return instance;
        }

        // Client management
        bool initializeClient()
        {
			std::string clientName = "kolosal-ai";
			std::string version = mcp::MCP_VERSION;

            try {
                if (m_clientType == ClientType::SSE) {
                    if (!m_sseClient) {
                        m_sseClient = std::make_unique<mcp::sse_client>(m_sseHost, m_ssePort);

                        // Set default capabilities
                        mcp::json capabilities = {
                            {"roots", {{"listChanged", true}}}
                        };
                        m_sseClient->set_capabilities(capabilities);

                        // Set timeout
                        m_sseClient->set_timeout(m_timeout);
                    }

                    m_initialized = m_sseClient->initialize(clientName, version);
                }
                else { // ClientType::STDIO
                    if (!m_stdioClient) {
                        m_stdioClient = std::make_unique<mcp::stdio_client>(m_stdioCommand, m_stdioEnvVars);
                    }

                    m_initialized = m_stdioClient->initialize(clientName, version);
                }

                if (m_initialized) {
                    refreshAvailableTools();
                }

                return m_initialized;
            }
            catch (const std::exception& e) {
                std::cerr << "Error initializing MCP client: " << e.what() << std::endl;
                return false;
            }
        }

        std::string ToolManager::formatPromptWithTools(const std::string& originalSystemPrompt) const
        {
            if (!m_initialized || m_availableTools.empty()) {
                return originalSystemPrompt;
            }

            std::stringstream toolsJson;
            toolsJson << "[\n";

            for (size_t i = 0; i < m_availableTools.size(); ++i) {
                const auto& tool = m_availableTools[i];

                toolsJson << "    {\n";
                toolsJson << "        \"name\": \"" << tool.name << "\",\n";
                toolsJson << "        \"description\": \"" << tool.description << "\",\n";
                toolsJson << "        \"parameters\": ";

                // Convert the parameters schema to JSON string
                std::string paramsJson = tool.parameters_schema.dump(4);

                // Indent each line of the parameters JSON
                std::istringstream paramStream(paramsJson);
                std::string line;
                bool firstLine = true;

                while (std::getline(paramStream, line)) {
                    if (firstLine) {
                        toolsJson << line << "\n";
                        firstLine = false;
                    }
                    else {
                        toolsJson << "        " << line << "\n";
                    }
                }

                toolsJson << "    }";

                if (i < m_availableTools.size() - 1) {
                    toolsJson << ",";
                }

                toolsJson << "\n";
            }

            toolsJson << "]";

            // The tool capabilities part of system prompt
            std::stringstream toolCapabilitiesPrompt;
            toolCapabilitiesPrompt << "\n\nYou are an expert in composing functions. You are given a question and a set of possible functions. ";
            toolCapabilitiesPrompt << "Based on the question, you will need to make one or more function/tool calls to achieve the purpose. ";
            toolCapabilitiesPrompt << "If none of the function can be used, point it out. If the given question lacks the parameters required by the function, ";
            toolCapabilitiesPrompt << "also point it out. You should only return the function call in tools call sections.\n\n";
            toolCapabilitiesPrompt << "If you decide to invoke any of the function(s), you MUST put it in the format of [func_name1(params_name1=params_value1, params_name2=params_value2...), func_name2(params)]\n";
            toolCapabilitiesPrompt << "You SHOULD NOT include any other text in the response.\n\n";
            toolCapabilitiesPrompt << "Here is a list of functions in JSON format that you can invoke.\n\n";
            toolCapabilitiesPrompt << toolsJson.str();

            // Append tool capabilities to the original system prompt
            std::string formattedSystemPrompt = originalSystemPrompt;
            if (!formattedSystemPrompt.empty() && formattedSystemPrompt.back() != '\n') {
                formattedSystemPrompt += "\n";
            }
            formattedSystemPrompt += toolCapabilitiesPrompt.str();

            return formattedSystemPrompt;
        }

        void setClientType(ClientType type)
        {
            if (m_clientType != type) {
                m_clientType = type;
                m_initialized = false;

                // Reset available tools
                {
                    std::lock_guard<std::mutex> lock(m_toolsMutex);
                    m_availableTools.clear();
                }
            }
        }

        ClientType getClientType() const
        {
            return m_clientType;
        }

        bool isInitialized() const { return m_initialized; }

        // SSE client configuration
        void setSseEndpoint(const std::string& host, int port)
        {
            if (m_sseHost != host || m_ssePort != port) {
                m_sseHost = host;
                m_ssePort = port;

                if (m_clientType == ClientType::SSE) {
                    m_sseClient.reset();
                    m_initialized = false;
                }
            }
        }

        // STDIO client configuration
        void setStdioCommand(const std::string& command, const mcp::json& envVars)
        {
            if (m_stdioCommand != command || m_stdioEnvVars != envVars) {
                m_stdioCommand = command;
                m_stdioEnvVars = envVars;

                if (m_clientType == ClientType::STDIO) {
                    m_stdioClient.reset();
                    m_initialized = false;
                }
            }
        }

        // Tool management
        bool refreshAvailableTools()
        {
            if (!m_initialized) {
                return false;
            }

            try {
                std::vector<mcp::tool> tools;

                if (m_clientType == ClientType::SSE) {
                    tools = m_sseClient->get_tools();
                }
                else { // ClientType::STDIO
                    tools = m_stdioClient->get_tools();
                }

                {
                    std::lock_guard<std::mutex> lock(m_toolsMutex);
                    m_availableTools = std::move(tools);
                }

                return true;
            }
            catch (const std::exception& e) {
                std::cerr << "Error refreshing available tools: " << e.what() << std::endl;
                return false;
            }
        }

        std::vector<mcp::tool> getAvailableTools() const
        {
            return m_availableTools;
        }

        // Tool execution
        std::vector<ToolResult> executeTools(const std::vector<ToolCall>& toolCalls)
        {
            std::vector<ToolResult> results;
            results.reserve(toolCalls.size());

            for (const auto& toolCall : toolCalls) {
                results.push_back(executeToolCall(toolCall));
            }

            return results;
        }

        std::future<std::vector<ToolResult>> executeToolsAsync(const std::vector<ToolCall>& toolCalls)
        {
            return std::async(std::launch::async, [this, toolCalls]() {
                return executeTools(toolCalls);
                });
        }

        static inline const std::regex s_toolCallBlockRegex{ R"(\[(.*?)\])" };
        static inline const std::regex s_functionCallRegex{ R"((\w+)\s*\(\s*((?:[^()]|(?:\([^()]*\)))*)\s*\))" };
        static inline const std::regex s_paramRegex{ R"((\w+)\s*=\s*([^,]+))" };

        // Tool call detection and extraction
        static bool containsToolCall(const std::string& text) {
            // Fast pre-check for essential characters before using regex
            if (text.find('[') == std::string::npos ||
                text.find('(') == std::string::npos ||
                text.find(')') == std::string::npos ||
                text.find(']') == std::string::npos) {
                return false;
            }

            // Use precompiled regex for one-time search
            return std::regex_search(text, s_toolCallBlockRegex) &&
                std::regex_search(text, s_functionCallRegex);
        }

        static std::vector<ToolCall> extractToolCalls(const std::string& text) {
            std::vector<ToolCall> result;

            std::smatch blockMatch;
            std::string::const_iterator searchStart(text.cbegin());

            while (std::regex_search(searchStart, text.cend(), blockMatch, s_toolCallBlockRegex)) {
                size_t blockStartIndex = std::distance(text.cbegin(), blockMatch[0].first);
                size_t blockEndIndex = blockStartIndex + blockMatch[0].length() - 1;

                std::string blockContent = blockMatch[1].str();

                // Extract function calls from block content
                std::smatch funcMatch;
                std::string::const_iterator funcSearchStart(blockContent.cbegin());

                while (std::regex_search(funcSearchStart, blockContent.cend(), funcMatch, s_functionCallRegex)) {
                    ToolCall toolCall;
                    toolCall.func_name = funcMatch[1].str();
                    toolCall.start_index = blockStartIndex;
                    toolCall.end_index = blockEndIndex;

                    std::string paramsStr = funcMatch[2].str();
                    parseParameters(paramsStr, toolCall.params);

                    result.push_back(toolCall);
                    funcSearchStart = funcMatch.suffix().first;
                }

                searchStart = blockMatch.suffix().first;
            }

            return result;
        }

        static void printToolCalls(const std::vector<ToolCall>& toolCalls) {
            if (toolCalls.empty()) {
                std::cout << "No tool calls found." << std::endl;
                return;
            }

            for (const auto& tool : toolCalls) {
                std::cout << "Function Name: " << tool.func_name << std::endl;
                std::cout << "Start Index: " << tool.start_index << std::endl;
                std::cout << "End Index: " << tool.end_index << std::endl;

                std::cout << "Parameters:" << std::endl;
                for (const auto& param : tool.params) {
                    std::cout << "  " << param.first << " = " << param.second << std::endl;
                }

                std::cout << "Output: \"" << tool.output << "\"" << std::endl;
                std::cout << "------------------------" << std::endl;
            }
        }

        // Utility for applying tool results to text
        std::string replaceToolCallsWithResults(const std::string& text, const std::vector<ToolCall>& toolCalls)
        {
            std::string result = text;

            // Sort tool calls by end index (descending) to avoid index shifts when replacing
            std::vector<ToolCall> sortedCalls = toolCalls;
            std::sort(sortedCalls.begin(), sortedCalls.end(), [](const ToolCall& a, const ToolCall& b) {
                return a.end_index > b.end_index;
                });

            for (const auto& toolCall : sortedCalls) {
                if (toolCall.start_index >= 0 && toolCall.end_index >= toolCall.start_index &&
                    toolCall.end_index < static_cast<int>(result.size())) {

                    // Replace tool call with its output
                    result.replace(
                        toolCall.start_index,
                        toolCall.end_index - toolCall.start_index + 1,
                        toolCall.output
                    );
                }
            }

            return result;
        }

    private:
        // Private constructor for singleton pattern
        ToolManager()
            : m_clientType(ClientType::SSE)
            , m_sseHost("localhost")
            , m_ssePort(8888)
            , m_timeout(10)
            , m_stdioCommand("")
            , m_initialized(false)
        {
            // Default json configuration for environment variables
            m_stdioEnvVars = mcp::json::object();
        }

        // Delete copy/move constructors and assignment operators
        ToolManager(const ToolManager&) = delete;
        ToolManager& operator=(const ToolManager&) = delete;
        ToolManager(ToolManager&&) = delete;
        ToolManager& operator=(ToolManager&&) = delete;

        // Helper methods
        static void parseParameters(const std::string& paramsStr, std::map<std::string, std::string>& params) {
            std::string::size_type pos = 0;

            while (pos < paramsStr.length()) {
                // Skip leading whitespace
                while (pos < paramsStr.length() && std::isspace(paramsStr[pos])) pos++;
                if (pos >= paramsStr.length()) break;

                // Find parameter name
                std::string::size_type nameStart = pos;
                while (pos < paramsStr.length() && paramsStr[pos] != '=') pos++;
                if (pos >= paramsStr.length()) break;

                std::string name = paramsStr.substr(nameStart, pos - nameStart);
                trimString(name);

                // Skip the equals sign
                pos++;

                // Find parameter value (handle commas within parentheses)
                std::string::size_type valueStart = pos;
                int parenDepth = 0;
                bool inQuotes = false;

                while (pos < paramsStr.length()) {
                    char c = paramsStr[pos];

                    if (c == '"' && (pos == 0 || paramsStr[pos - 1] != '\\')) {
                        inQuotes = !inQuotes;
                    }
                    else if (!inQuotes) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) break;
                    }

                    pos++;
                }

                std::string value = paramsStr.substr(valueStart, pos - valueStart);
                trimString(value);

                params[name] = value;

                // Skip past the comma
                if (pos < paramsStr.length() && paramsStr[pos] == ',') pos++;
            }
        }

        static void trimString(std::string& str) {
            // Remove leading whitespace
            str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
                return !std::isspace(ch);
                }));

            // Remove trailing whitespace
            str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
                }).base(), str.end());
        }

        static mcp::json autoConvertValue(const std::string& value) {
            // Trim the value to handle whitespace
            std::string trimmed = value;
            trimString(trimmed);

            // Check if it's a boolean
            if (trimmed == "true") return true;
            if (trimmed == "false") return false;

            // Check if it's null
            if (trimmed == "null") return nullptr;

            // Remove surrounding quotation marks if present
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                trimmed = trimmed.substr(1, trimmed.size() - 2);
            }

            // Try to convert to integer
            try {
                // Check if it looks like an integer (no decimal point)
                if (trimmed.find('.') == std::string::npos &&
                    std::all_of(trimmed.begin(), trimmed.end(), [](char c) {
                        return std::isdigit(c) || c == '-' || c == '+';
                        })) {
                    return std::stoll(trimmed);
                }

                // Try to convert to double (floating point)
                return std::stod(trimmed);
            }
            catch (const std::exception&) {
                // If conversion fails, return as string
                return trimmed;
            }
        }

        // Execute a single tool call
        ToolResult executeToolCall(const ToolCall& toolCall)
        {
            ToolResult result;
            result.toolCall = toolCall;
            result.success = false;

            if (!m_initialized) {
                result.error = "MCP client not initialized";
                return result;
            }

            try {
                mcp::json params = mcp::json::object();

                // Convert parameters from map to JSON with automatic type detection
                for (const auto& [key, value] : toolCall.params) {
                    params[key] = autoConvertValue(value);
                }

                mcp::json response;

                if (m_clientType == ClientType::SSE) {
                    response = m_sseClient->call_tool(toolCall.func_name, params);
                }
                else { // ClientType::STDIO
                    response = m_stdioClient->call_tool(toolCall.func_name, params);
                }

                // Extract text content from the response
                if (response.contains("content") && response["content"].is_array() && !response["content"].empty()) {
                    if (response["content"][0].contains("text")) {
                        result.result = response["content"][0]["text"].get<std::string>();
                        result.success = true;
                    }
                }

                if (!result.success) {
                    result.error = "Invalid response format from tool call";
                }
            }
            catch (const std::exception& e) {
                result.error = std::string("Tool call error: ") + e.what();
            }

            return result;
        }

        // Client members
        ClientType m_clientType;
        std::unique_ptr<mcp::sse_client> m_sseClient;
        std::unique_ptr<mcp::stdio_client> m_stdioClient;

        // SSE configuration
        std::string m_sseHost;
        int m_ssePort;
        int m_timeout;

        // STDIO configuration
        std::string m_stdioCommand;
        mcp::json m_stdioEnvVars;

        // Tool cache
        std::vector<mcp::tool> m_availableTools;
        std::mutex m_toolsMutex;

        // State tracking
        bool m_initialized;
    };
}