#pragma once

#include "chat_history.hpp"
#include "crypto/crypto.hpp"

#include <future>
#include <shared_mutex>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace Chat
{
    /**
     * @brief Interface for chat persistence strategies
     */
    class IChatPersistence 
    {
    public:
        virtual ~IChatPersistence() = default;
        virtual std::future<bool> saveChat(const ChatHistory& chat) = 0;
        virtual std::future<bool> deleteChat(const std::string& chatName) = 0;
		virtual std::future<bool> deleteKvChat(const std::string& chatName) = 0;
		virtual std::future<bool> renameKvChat(const std::string& oldChatName, const std::string& newChatName) = 0;
        virtual std::future<std::vector<ChatHistory>> loadAllChats() = 0;
		virtual std::filesystem::path getChatPath(const std::string& chatName) const = 0;
		virtual std::filesystem::path getKvChatPath(const std::string& chatName) const = 0;
    };

    /**
     * @brief File-based chat persistence implementation using AES-GCM encryption
     */
    class FileChatPersistence : public IChatPersistence 
    {
    public:
        explicit FileChatPersistence(std::filesystem::path basePath, std::array<uint8_t, 32> key)
            : m_basePath(std::move(basePath)), m_key(key) 
        {
			// Create base path if it doesn't exist
			if (!std::filesystem::exists(m_basePath))
			{
				std::filesystem::create_directory(m_basePath);
			}
        }

        std::future<bool> saveChat(const ChatHistory& chat) override 
        {
            return std::async(std::launch::async, [this, chat]() {
                std::unique_lock<std::shared_mutex> lock(m_ioMutex);
                return saveEncryptedChat(chat);
                });
        }

        std::future<bool> deleteChat(const std::string& chatName) override 
        {
            return std::async(std::launch::async, [this, chatName]() {
                std::unique_lock<std::shared_mutex> lock(m_ioMutex);
                try 
                {
                    std::filesystem::remove(getChatPath(chatName));
                    return true;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[FileChatPersistence] Failed to delete chat: " << chatName << "\n";
                    return false;
                }
                });
        }

        std::future<bool> deleteKvChat(const std::string& chatName) {
            return std::async(std::launch::async, [this, chatName]() {
                std::unique_lock<std::shared_mutex> lock(m_ioMutex);
                bool allDeleted = true;

                try {
                    for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
                        if (entry.is_regular_file()) {
                            std::string fileName = entry.path().filename().string();

                            // First, ensure the filename ends with ".bin"
                            if (fileName.size() >= 4 &&
                                fileName.compare(fileName.size() - 4, 4, ".bin") == 0)
                            {
                                // Remove the ".bin" extension.
                                std::string baseName = fileName.substr(0, fileName.size() - 4);

                                // Find the last occurrence of '@' in the baseName.
                                auto atPos = baseName.rfind('@');
                                if (atPos != std::string::npos) {
                                    // Extract the chat name portion (everything before the last '@')
                                    std::string fileChatName = baseName.substr(0, atPos);

                                    // Compare with the provided chatName.
                                    if (fileChatName == chatName) {
                                        try {
                                            if (!std::filesystem::remove(entry.path())) {
                                                std::cerr << "[FileChatPersistence] Failed to remove file: "
                                                    << entry.path() << "\n";
                                                allDeleted = false;
                                            }
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "[FileChatPersistence] Exception deleting file "
                                                << entry.path() << ": " << e.what() << "\n";
                                            allDeleted = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "[FileChatPersistence] Exception iterating directory: "
                        << e.what() << "\n";
                    return false;
                }

                return allDeleted;
                });
        }

        std::future<bool> renameKvChat(const std::string& oldChatName, const std::string& newChatName) {
            return std::async(std::launch::async, [this, oldChatName, newChatName]() {
                std::unique_lock<std::shared_mutex> lock(m_ioMutex);
                bool allRenamed = true;
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
                        if (entry.is_regular_file()) {
                            std::string fileName = entry.path().filename().string();
                            // Check that the file ends with ".bin"
                            if (fileName.size() >= 4 &&
                                fileName.compare(fileName.size() - 4, 4, ".bin") == 0)
                            {
                                // Remove the ".bin" extension.
                                std::string baseName = fileName.substr(0, fileName.size() - 4);

                                // Find the last occurrence of '@'
                                auto atPos = baseName.rfind('@');
                                if (atPos != std::string::npos) {
                                    // Extract the chat name portion (everything before the last '@')
                                    std::string fileChatName = baseName.substr(0, atPos);

                                    // If it matches the old chat name, we need to rename it.
                                    if (fileChatName == oldChatName) {
                                        // The part starting with '@' (i.e. the model name and separator)
                                        std::string modelPart = baseName.substr(atPos);
                                        // Build the new file name: newChatName + modelPart + ".bin"
                                        std::string newFileName = newChatName + modelPart + ".bin";
                                        std::filesystem::path newPath = std::filesystem::absolute(
                                            std::filesystem::path(m_basePath) / newFileName);
                                        try {
                                            std::filesystem::rename(entry.path(), newPath);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "[FileChatPersistence] Exception renaming file "
                                                << entry.path() << " to " << newPath << ": " << e.what() << "\n";
                                            allRenamed = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "[FileChatPersistence] Exception iterating directory for renaming: "
                        << e.what() << "\n";
                    return false;
                }
                return allRenamed;
                });
        }

        std::future<std::vector<ChatHistory>> loadAllChats() override 
        {
            return std::async(std::launch::async, [this]() {
                std::shared_lock<std::shared_mutex> lock(m_ioMutex);
                return loadEncryptedChats();
                });
        }

        std::filesystem::path getChatPath(const std::string& chatName) const override
        {
            return std::filesystem::absolute(
                std::filesystem::path(m_basePath) / (chatName + ".chat"));
        }

        std::filesystem::path getKvChatPath(const std::string& chatName) const override
		{
			return std::filesystem::absolute(
				std::filesystem::path(m_basePath) / (chatName + ".bin"));
		}

    private:
        const   std::filesystem::path   m_basePath;
        const   std::array<uint8_t, 32> m_key;
        mutable std::shared_mutex       m_ioMutex;

        bool saveEncryptedChat(const ChatHistory& chat) 
        {
            try {
                // Use the existing to_json serialization
                nlohmann::json chatJson;
                to_json(chatJson, chat);

                // Convert JSON to binary
                std::string jsonStr = chatJson.dump();
                std::vector<uint8_t> plaintext(jsonStr.begin(), jsonStr.end());

                // Encrypt the data
                auto encrypted = Crypto::encrypt(plaintext, m_key);

                // Save to file
				std::filesystem::path chatPath = getChatPath(chat.name);
                std::ofstream file(chatPath, std::ios::binary);
                if (!file) {
                    return false;
                }

                file.write(reinterpret_cast<const char*>(encrypted.data()),
                    encrypted.size());

                return true;
            }
            catch (const std::exception& e) {
                // TODO: Log error details here
                return false;
            }
        }

        std::vector<ChatHistory> loadEncryptedChats() 
        {
            std::vector<ChatHistory> chats;

            try {
                for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
                    if (entry.path().extension() == ".chat") {
                        // Read encrypted file
                        std::ifstream file(entry.path(), std::ios::binary);
                        if (!file) continue;

                        std::vector<uint8_t> encrypted(
                            (std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>()
                        );

                        // Decrypt the data
                        auto plaintext = Crypto::decrypt(encrypted, m_key);

                        // Parse JSON using the existing from_json serialization
                        std::string jsonStr(plaintext.begin(), plaintext.end());
                        auto chatJson = nlohmann::json::parse(jsonStr);

                        ChatHistory chat;
                        from_json(chatJson, chat);
                        chats.push_back(std::move(chat));
                    }
                }
            }
            catch (const std::exception& e) {
                // TODO: Log error details here
            }

            return chats;
        }
    };

} // namespace Chat