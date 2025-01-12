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

namespace Chat
{
    /**
     * @brief Singleton ChatManager class with thread-safe operations
     */
    class ChatManager 
    {
    public:
        static ChatManager& getInstance() 
        {
            static ChatManager instance(std::make_unique<FileChatPersistence>("chats", Crypto::generateKey()));
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
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_chatNameToIndex.find(name);
            if (it == m_chatNameToIndex.end()) 
            {
                return false;
            }

            std::string oldChat = m_currentChatName.value_or("");
            m_currentChatName = name;
            m_currentChatIndex = it->second;

            return true;
        }

        std::future<bool> renameCurrentChat(const std::string& newName)
        {
            return std::async(std::launch::async, [this, newName]() {
                if (!validateChatName(newName)) 
                {
                    return false;
                }

                std::unique_lock<std::shared_mutex> lock(m_mutex);

                if (!m_currentChatName) 
                {
                    return false;
                }

                if (m_chatNameToIndex.find(newName) != m_chatNameToIndex.end()) 
                {
                    return false;
                }

                size_t currentIdx = m_currentChatIndex;
                if (currentIdx >= m_chats.size()) 
                {
                    return false;
                }

                std::string oldName = m_chats[currentIdx].name;
                m_chats[currentIdx].name = newName;
                m_chats[currentIdx].lastModified = static_cast<int>(std::time(nullptr));
                
                // Update indices
                m_chatNameToIndex.erase(oldName);
                m_chatNameToIndex[newName] = currentIdx;
                m_currentChatName = newName;

                // Save changes
                auto chat = m_chats[currentIdx];
                auto saveResult = m_persistence->saveChat(chat).get();
                if (saveResult) 
                {
                    m_persistence->deleteChat(oldName).get();
                }

                return saveResult;
            });
        }

		std::future<bool> clearCurrentChat()
		{
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

		void updateChat(const std::string& chatName, const ChatHistory& chat)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			auto it = m_chatNameToIndex.find(chatName);
			if (it == m_chatNameToIndex.end())
			{
				std::cerr << "[ChatManager] Chat not found: " << chatName << std::endl;
				return;
			}
			m_chats[it->second] = chat;
			// Launch async save operation
			std::async(std::launch::async, [this, chat]() {
				m_persistence->saveChat(chat);
				});
		}

        // Async operations
        std::future<bool> createNewChat(const std::string& name) 
        {
            return std::async(std::launch::async, [this, name]() {
                if (!validateChatName(name)) 
                {
                    return false;
                }

                std::unique_lock<std::shared_mutex> lock(m_mutex);
                if (m_chatNameToIndex.find(name) != m_chatNameToIndex.end()) 
                {
                    return false;
                }

                const int newTimestamp = static_cast<int>(std::time(nullptr));
                ChatHistory newChat{
                    static_cast<int>(m_chats.size() + 1),
                    newTimestamp,
                    name,
                    {}
                };

                size_t newIndex = m_chats.size();
                m_chats.push_back(newChat);
                m_chatNameToIndex[name] = newIndex;

                // Add to sorted indices
                m_sortedIndices.insert({newTimestamp, newIndex, name});

                return m_persistence->saveChat(newChat).get();
            });
        }

        std::future<bool> deleteChat(const std::string& name) 
        {
            return std::async(std::launch::async, [this, name]() {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                
                auto it = m_chatNameToIndex.find(name);
                if (it == m_chatNameToIndex.end()) 
                {
                    return false;
                }

                size_t indexToRemove = it->second;
                
                // Remove from sorted indices
                auto timestamp = m_chats[indexToRemove].lastModified;
                m_sortedIndices.erase({timestamp, indexToRemove, name});

                m_chats.erase(m_chats.begin() + indexToRemove);
                m_chatNameToIndex.erase(it);

                // Update indices
                updateIndicesAfterDeletion(indexToRemove);

                if (m_currentChatIndex == indexToRemove) 
                {
                    m_currentChatName = std::nullopt;
                    m_currentChatIndex = 0;
                }
                else if (m_currentChatIndex > indexToRemove) 
                {
                    m_currentChatIndex--;
                }

                return m_persistence->deleteChat(name).get();
            });
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

                // Launch async save operation without blocking
                auto chat = *it;
                std::async(std::launch::async, [this, chat]() {
                    m_persistence->saveChat(chat);
                    });
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

        // Validation helpers
        static bool validateChatName(const std::string& name) 
        {
            if (name.empty() || name.length() > 256) return false;
            const std::string invalidChars = R"(<>:"/\|?*)";
            return name.find_first_of(invalidChars) == std::string::npos;
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

        static inline const std::string DEFAULT_CHAT_NAME = "New Chat";

        std::unique_ptr<IChatPersistence> m_persistence;
        std::vector<ChatHistory> m_chats;
        std::unordered_map<std::string, size_t> m_chatNameToIndex;
        std::set<ChatIndex> m_sortedIndices;
        std::optional<std::string> m_currentChatName;
        size_t m_currentChatIndex;
        mutable std::shared_mutex m_mutex;
		std::unordered_map<int, int> m_chatInferenceJobIdMap;
    };

    inline void initializeChatManager() {
        // The singleton will be created with default persistence on first access
        ChatManager::getInstance();
    }

    inline void initializeChatManagerWithCustomPersistence(std::unique_ptr<IChatPersistence> persistence) {
        ChatManager::getInstance().initialize(std::move(persistence));
    }

} // namespace Chat