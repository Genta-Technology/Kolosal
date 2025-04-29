// TODO: Implement the Observer pattern in the ChatManager class

#pragma once

#include "chat_persistence.hpp"

#include <vector>
#include <string>
#include <future>
#include <shared_mutex>
#include <optional>
#include <memory>
#include <set>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Chat
{
    static std::unordered_map<std::string, bool> gThinkToggleMap;

    /**
     * @brief Singleton ChatManager class with thread-safe operations
     */
    class ChatManager 
    {
    public:
        static ChatManager& getInstance() 
        {
            static ChatManager instance(std::make_unique<FileChatPersistence>(
                getChatPath().value_or("chat"), Crypto::generateKey()));
            return instance;
        }

        // Delete copy and move operations
        ChatManager(const ChatManager&) = delete;
        ChatManager& operator=(const ChatManager&) = delete;
        ChatManager(ChatManager&&) = delete;
        ChatManager& operator=(ChatManager&&) = delete;

    private:
        // Helper struct to maintain sorted indices
        struct ChatIndex {
            int lastModified;
            size_t index;
            std::string name;

            bool operator<(const ChatIndex& other) const {
                // Sort by lastModified (descending) and then by name for stable sorting
                return lastModified > other.lastModified ||
                    (lastModified == other.lastModified && name < other.name);
            }
        };

    public:
        void initialize(std::unique_ptr<IChatPersistence> persistence) 
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_persistence = std::move(persistence);
            m_currentChatName = std::nullopt;
            m_currentChatIndex = 0;
            loadChatsAsync();
        }

        std::optional<std::string> getCurrentChatName() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_currentChatName;
        }

        bool switchToChat(const std::string& name)
        {
            auto it = m_chatNameToIndex.find(name);
            if (it == m_chatNameToIndex.end()) 
            {
                return false;
            }

            std::string oldChat = m_currentChatName.value_or("");
            m_currentChatName = name;
            m_currentChatIndex = it->second;

            gThinkToggleMap.clear();

            return true;
        }

        std::future<bool> renameCurrentChat(const std::string& newName)
        {
            return std::async(std::launch::async, [this, newName]() {
                if (!validateChatName(newName))
                {
                    std::cerr << "[ChatManager] [ERROR] " << newName << " is not valid" << std::endl;
                    return false;
                }

                std::unique_lock<std::shared_mutex> lock(m_mutex);

                if (!m_currentChatName)
                {
                    std::cerr << "[ChatManager] No current chat selected.\n";
                    return false;
                }

                // Generate a unique name if the requested name already exists
                std::string uniqueName = newName;
                int counter = 1;

                while (m_chatNameToIndex.find(uniqueName) != m_chatNameToIndex.end())
                {
                    uniqueName = newName + " (" + std::to_string(counter) + ")";
                    counter++;
                }

                size_t currentIdx = m_currentChatIndex;
                if (currentIdx >= m_chats.size())
                {
                    std::cerr << "[ChatManager] Invalid chat index: " << currentIdx << std::endl;
                    return false;
                }

                std::string oldName = m_chats[currentIdx].name;
                m_chats[currentIdx].name = uniqueName;
                m_chats[currentIdx].lastModified = static_cast<int>(std::time(nullptr));

                // Update indices
                m_chatNameToIndex.erase(oldName);
                m_chatNameToIndex[uniqueName] = currentIdx;
                m_currentChatName = uniqueName;

                // Save changes
                auto chat = m_chats[currentIdx];
                auto saveResult = m_persistence->saveChat(chat).get();
                if (saveResult)
                {
                    m_persistence->deleteChat(oldName).get();
                    m_persistence->renameKvChat(oldName, uniqueName).get();
                }

                return saveResult;
                });
        }

		std::future<bool> clearCurrentChat()
		{
            gThinkToggleMap.clear();

			return std::async(std::launch::async, [this]() {
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				if (!m_currentChatName || m_currentChatIndex >= m_chats.size())
				{
					return false;
				}
				m_chats[m_currentChatIndex].messages.clear();
				m_chats[m_currentChatIndex].lastModified = static_cast<int>(std::time(nullptr));
				// Launch async save operation
				auto chat = m_chats[m_currentChatIndex];
				return m_persistence->saveChat(chat).get();
				});
		}

        std::optional<ChatHistory> getCurrentChat() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentChatName || m_currentChatIndex >= m_chats.size()) 
            {
                return std::nullopt;
            }
            return m_chats[m_currentChatIndex];
        }

        void addMessageToCurrentChat(const Message& message)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentChatName || m_currentChatIndex >= m_chats.size()) 
            {
				std::cerr << "[ChatManager] No current chat selected.\n";
                return;
            }

            const int newTimestamp = static_cast<int>(std::time(nullptr));
            updateChatTimestamp(m_currentChatIndex, newTimestamp);

            m_chats[m_currentChatIndex].messages.push_back(message);

            // Launch async save operation
            auto chat = m_chats[m_currentChatIndex];
            std::async(std::launch::async, [this, chat]() {
                m_persistence->saveChat(chat);
            });
        }

		void updateCurrentChat(const ChatHistory& chat)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			if (!m_currentChatName || m_currentChatIndex >= m_chats.size())
			{
				std::cerr << "[ChatManager] No current chat selected.\n";
				return;
			}
			m_chats[m_currentChatIndex] = chat;
			// Launch async save operation
			std::async(std::launch::async, [this, chat]() {
				m_persistence->saveChat(chat);
				});
		}

		bool updateChat(const std::string& chatName, const ChatHistory& chat)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			auto it = m_chatNameToIndex.find(chatName);
			if (it == m_chatNameToIndex.end())
			{
				std::cerr << "[ChatManager] Chat not found: " << chatName << std::endl;
				return false;
			}
			m_chats[it->second] = chat;
            return true;
		}

        bool saveChat(const std::string& chatName)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_chatNameToIndex.find(chatName);
            if (it == m_chatNameToIndex.end())
            {
                std::cerr << "[ChatManager] Chat not found: " << chatName << std::endl;
                return false;
            }
            auto chat = m_chats[it->second];
			return m_persistence->saveChat(chat).get();
        }

        std::optional<std::string> createNewChat(const std::string& name)
        {
            std::string newName = name;

            std::unique_lock<std::shared_mutex> lock(m_mutex);
            while (m_chatNameToIndex.find(newName) != m_chatNameToIndex.end())
            {
                newName = name + " (" + std::to_string(counter) + ")";
                counter++;
            }

            if (!validateChatName(newName))
            {
                std::cerr << "[ChatManager] [ERROR] " << newName << " is not valid" << std::endl;
                return std::nullopt;
            }

            const int newTimestamp = static_cast<int>(std::time(nullptr));
            ChatHistory newChat{
                static_cast<int>(counter) + newTimestamp,
                newTimestamp,
                newName,
                {}
            };

            size_t newIndex = m_chats.size();
            m_chats.push_back(newChat);
            m_chatNameToIndex[newName] = newIndex;

            // Add to sorted indices
            m_sortedIndices.insert({ newTimestamp, newIndex, newName });

            // Switch to the new chat
            switchToChat(newName);

            if (m_persistence->saveChat(newChat).get())
            {
                std::cout << "[ChatManager] Created new chat: " << newName << std::endl;
            }

            return newName;
        }

        bool deleteChat(const std::string& name) 
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            auto it = m_chatNameToIndex.find(name);
            if (it == m_chatNameToIndex.end())
            {
                std::cerr << "[ChatManager] Chat not found: " << name << std::endl;
                return false;
            }

            size_t indexToRemove = it->second;

            // Remove from sorted indices
            auto timestamp = m_chats[indexToRemove].lastModified;
            m_sortedIndices.erase({ timestamp, indexToRemove, name });

            m_chats.erase(m_chats.begin() + indexToRemove);
            m_chatNameToIndex.erase(it);

            // Update indices
            updateIndicesAfterDeletion(indexToRemove);

            if (m_chats.empty())
            {
                createDefaultChat();
            }
            // i know it looks ugly, but it works
            else if (m_currentChatIndex == indexToRemove)
            {
                // Select the most recent chat (first in sorted indices)
                auto mostRecent = m_sortedIndices.begin();
                switchToChat(mostRecent->name);
            }
            else if (m_currentChatIndex > indexToRemove)
            {
                m_currentChatIndex--;
            }

            if (!m_persistence->deleteChat(name).get())
            {
				std::cerr << "[ChatManager] Failed to delete chat: " << name << std::endl;
				return false;
            }

            lock.unlock();

            if (!m_persistence->deleteKvChat(name).get())
            {
				std::cerr << "[ChatManager] Failed to delete kv chat: " << name << std::endl;
				return false;
            }

            std::cout << "[ChatManager] Deleted chat: " << name << std::endl;

            return true;
        }

        void deleteMessage(const std::string& chatName, const Message& message) {
            // Lock the mutex to ensure thread-safe access.
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            // Find the chat by its name.
            auto chatIt = std::find_if(m_chats.begin(), m_chats.end(),
                [&chatName](const auto& chat) { return chat.name == chatName; });

            if (chatIt != m_chats.end()) {
                // Search for the message with the matching id.
                auto& messages = chatIt->messages;
                auto msgIt = std::find_if(messages.begin(), messages.end(),
                    [&message](const Message& m) { return m.id == message.id; });

                // If the message was found, erase it from the chat.
                if (msgIt != messages.end()) {
                    messages.erase(msgIt);
                    chatIt->lastModified = static_cast<int>(std::time(nullptr));
                }
            }
        }

        void deleteMessage(const std::string& chatName, int index) {
            // Lock the mutex to ensure thread-safe access.
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            // Locate the chat by its name.
            auto chatIt = std::find_if(m_chats.begin(), m_chats.end(),
                [&chatName](const auto& chat) { return chat.name == chatName; });

            if (chatIt != m_chats.end()) {
                // Check if the index is valid.
                if (index >= 0 && index < static_cast<int>(chatIt->messages.size())) {
                    // Remove the message at the given index.
                    chatIt->messages.erase(chatIt->messages.begin() + index);
                    // Update the last modified timestamp.
                    chatIt->lastModified = static_cast<int>(std::time(nullptr));
                }
                else {
                    std::cerr << "[ChatManager] Invalid message index (" << index << ") for chat: " << chatName << "\n";
                }
            }
            else {
                std::cerr << "[ChatManager] Chat not found: " << chatName << "\n";
            }
        }

        void addMessage(const std::string& chatName, const Message& message) 
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = std::find_if(m_chats.begin(), m_chats.end(),
                [&chatName](const auto& chat) { return chat.name == chatName; });

            if (it != m_chats.end()) 
            {
                it->messages.push_back(message);
                it->lastModified = static_cast<int>(std::time(nullptr));
            }
        }

        void setMessageModelName(const std::string& chatName, const int& _index, const std::string& modelName)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = std::find_if(m_chats.begin(), m_chats.end(),
                [&chatName](const auto& chat) { return chat.name == chatName; });

			int index = _index;

            if (_index == -1)
            {
				index = it->messages.size() - 1;
            }

            if (it != m_chats.end())
            {
                if (index >= 0 && index < static_cast<int>(it->messages.size()))
                {
                    it->messages[index].modelName = modelName;
                    it->lastModified = static_cast<int>(std::time(nullptr));
                }
                else
                {
                    std::cerr << "[ChatManager] Invalid message index (" << index << ") for chat: " << chatName << "\n";
                }
            }
            else
            {
                std::cerr << "[ChatManager] Chat not found: " << chatName << "\n";
            }
        }

        // Thread-safe getters
        std::vector<ChatHistory> getChats() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            std::vector<ChatHistory> sortedChats;
            sortedChats.reserve(m_chats.size());

            std::unordered_set<size_t> seenIndices;

            // Use the sorted indices to return chats in order
            for (const auto& idx : m_sortedIndices)
            {
                if (seenIndices.insert(idx.index).second)
                {
                    sortedChats.push_back(m_chats[idx.index]);
                }
            }
            return sortedChats;
        }

        std::optional<ChatHistory> getChat(const std::string& name) const 
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = std::find_if(m_chats.begin(), m_chats.end(),
                [&name](const auto& chat) { return chat.name == name; });
            return it != m_chats.end() ? std::optional<ChatHistory>(*it) : std::nullopt;
        }

		std::optional<ChatHistory> getChat(int index) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			if (index < 0 || index >= m_chats.size())
			{
				return std::nullopt;
			}
			return m_chats[index];
		}

		size_t getChatsSize() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_chats.size();
		}

		size_t getCurrentChatIndex() const
		{
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_currentChatIndex;
		}

        size_t getSortedChatIndex(const std::string& name) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            size_t sortedIndex = 0;
            for (const auto& idx : m_sortedIndices) 
            {
                if (idx.name == name) 
                {
                    return sortedIndex;
                }
                sortedIndex++;
            }
            return 0;
        }

        std::optional<ChatHistory> getChatByTimestamp(int timestamp) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = std::find_if(m_sortedIndices.begin(), m_sortedIndices.end(),
                [timestamp](const ChatIndex& idx) { return idx.lastModified == timestamp; });

            if (it != m_sortedIndices.end()) 
            {
                return m_chats[it->index];
            }
            return std::nullopt;
        }

		bool setCurrentJobId(int jobId)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			// set the current chat index to the job id
			m_chatInferenceJobIdMap[m_currentChatIndex] = jobId;
			return true;
		}

		bool removeJobId(int jobId)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			// remove the job id from the chat index
			for (auto& [chatIndex, chatJobId] : m_chatInferenceJobIdMap)
			{
				if (chatJobId == jobId)
				{
					m_chatInferenceJobIdMap[chatIndex] = -1;
					return true;
				}
			}
			return false;
		}

		int getCurrentJobId()
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			// get the job id for the current chat index
			return m_chatInferenceJobIdMap[m_currentChatIndex];
		}
        
		int getJobId(const std::string& chatName)
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			auto it = m_chatNameToIndex.find(chatName);
			if (it == m_chatNameToIndex.end())
			{
				return -1;
			}
			return m_chatInferenceJobIdMap[it->second];
		}

		std::string getChatNameByJobId(int jobId)
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			for (const auto& [chatIndex, chatJobId] : m_chatInferenceJobIdMap)
			{
				if (chatJobId == jobId)
				{
					return m_chats[chatIndex].name;
				}
			}
			return "";
		}

		auto getCurrentChatPath() const -> std::optional<std::filesystem::path>
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			if (!m_currentChatName || m_currentChatIndex >= m_chats.size())
			{
				return std::nullopt;
			}
			return m_persistence->getChatPath(m_chats[m_currentChatIndex].name);
		}

		auto getCurrentKvChatPath(std::string modelName, std::string modelVariant) const -> std::optional<std::filesystem::path>
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			if (!m_currentChatName || m_currentChatIndex >= m_chats.size())
			{
				return std::nullopt;
			}

			return m_persistence->getKvChatPath(m_chats[m_currentChatIndex].name + "@" + modelName + modelVariant);
		}

		static const std::string getDefaultChatName() { return DEFAULT_CHAT_NAME; }

    private:
        explicit ChatManager(std::unique_ptr<IChatPersistence> persistence)
            : m_persistence(std::move(persistence))
            , m_chats()
			, m_currentChatName(std::nullopt)
			, m_currentChatIndex(0)
			, m_chatNameToIndex()
        {
            loadChatsAsync();
        }

        static std::optional<std::filesystem::path> getChatPath()
        {
            HKEY hKey;

            // Try to open the registry key
            LONG result = RegOpenKeyExW(
                HKEY_CURRENT_USER,
                L"Software\\KolosalAI",
                0,
                KEY_READ,
                &hKey
            );

            if (result != ERROR_SUCCESS) {
				std::cerr << "[ChatManager] Failed to open registry key\n";
                return std::nullopt;
            }

            // Get the size of the value first
            DWORD dataSize = 0;
            result = RegQueryValueExW(
                hKey,
                L"ChatHistory_Dir",
                nullptr,
                nullptr,
                nullptr,
                &dataSize
            );

            if (result != ERROR_SUCCESS) {
                RegCloseKey(hKey);
				std::cerr << "[ChatManager] Failed to query registry value\n";
                return std::nullopt;
            }

            // Allocate buffer and read the value
            std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t) + 1);

            result = RegQueryValueExW(
                hKey,
                L"ChatHistory_Dir",
                nullptr,
                nullptr,
                reinterpret_cast<LPBYTE>(buffer.data()),
                &dataSize
            );

            RegCloseKey(hKey);

            if (result != ERROR_SUCCESS) {
				std::cerr << "[ChatManager] Failed to read registry value\n";
                return std::nullopt;
            }

            return std::filesystem::path(buffer.data());
        }

        // Validation helpers
        static bool validateChatName(const std::string& name) 
        {
            return !(name.empty() || name.length() > 256);
        }

        void updateChatTimestamp(size_t chatIndex, int newTimestamp)
        {
            // Remove old index
            auto oldTimestamp = m_chats[chatIndex].lastModified;
            m_sortedIndices.erase({ oldTimestamp, chatIndex, m_chats[chatIndex].name });

            // Update timestamp
            m_chats[chatIndex].lastModified = newTimestamp;

            // Add new index
            m_sortedIndices.insert({ newTimestamp, chatIndex, m_chats[chatIndex].name });
        }

        void updateIndicesAfterDeletion(size_t deletedIndex)
        {
            // Update chatNameToIndex
            for (auto& [name, index] : m_chatNameToIndex) 
            {
                if (index > deletedIndex) 
                {
                    index--;
                }
            }

            // Update sortedIndices
            std::set<ChatIndex> newSortedIndices;
            for (const auto& idx : m_sortedIndices) 
            {
                if (idx.index > deletedIndex) 
                {
                    newSortedIndices.insert({ idx.lastModified, idx.index - 1, idx.name });
                }
                else if (idx.index < deletedIndex) 
                {
                    newSortedIndices.insert(idx);
                }
            }
            m_sortedIndices = std::move(newSortedIndices);
        }

        bool chatExists(const std::string& name) const 
        {
            return std::any_of(m_chats.begin(), m_chats.end(),
                [&name](const auto& chat) { return chat.name == name; });
        }

        void loadChatsAsync() 
        {
            std::async(std::launch::async, [this]() {
                auto chats = m_persistence->loadAllChats().get();

                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_chats = std::move(chats);
                
                // Initialize indices
                m_chatNameToIndex.clear();
                m_sortedIndices.clear();
                
                for (size_t i = 0; i < m_chats.size(); ++i) 
                {
                    m_chatNameToIndex[m_chats[i].name] = i;
                    m_sortedIndices.insert({
                        m_chats[i].lastModified,
                        i,
                        m_chats[i].name
                    });
                }

                // Handle empty state or select most recent chat
                if (m_chats.empty()) 
                {
                    createDefaultChat();
                }
                else if (!m_currentChatName) 
                {
                    // Select the most recent chat (first in sorted indices)
                    auto mostRecent = m_sortedIndices.begin();
                    m_currentChatIndex = mostRecent->index;
                    m_currentChatName = mostRecent->name;
                }

				counter = m_sortedIndices.size();
            });
        }

        void createDefaultChat()
        {
            const int currentTime = static_cast<int>(std::time(nullptr));
            ChatHistory defaultChat{
                1,
                currentTime,
                DEFAULT_CHAT_NAME,
                {}
            };

            m_chats.push_back(defaultChat);
            m_chatNameToIndex[DEFAULT_CHAT_NAME] = 0;
            m_sortedIndices.insert({ currentTime, 0, DEFAULT_CHAT_NAME });

            m_persistence->saveChat(defaultChat);
            m_currentChatName = DEFAULT_CHAT_NAME;
            m_currentChatIndex = 0;
        }

        static inline const std::string DEFAULT_CHAT_NAME = "New_Chat";

        std::unique_ptr<IChatPersistence> m_persistence;
        std::vector<ChatHistory> m_chats;
        std::unordered_map<std::string, size_t> m_chatNameToIndex;
        std::set<ChatIndex> m_sortedIndices;
        std::optional<std::string> m_currentChatName;
        size_t m_currentChatIndex;
        mutable std::shared_mutex m_mutex;
		std::unordered_map<int, int> m_chatInferenceJobIdMap;
        int counter;
    };

    inline void initializeChatManager() {
        // The singleton will be created with default persistence on first access
        ChatManager::getInstance();
    }

    inline void initializeChatManagerWithCustomPersistence(std::unique_ptr<IChatPersistence> persistence) {
        ChatManager::getInstance().initialize(std::move(persistence));
    }

} // namespace Chat