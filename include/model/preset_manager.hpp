#pragma once

#include "preset_persistence.hpp"
#include "logger.hpp"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <set>
#include <shared_mutex>
#include <future>
#include <optional>

namespace Model
{

    class PresetManager
    {
    public:
        static PresetManager& getInstance()
        {
            static PresetManager instance(std::make_unique<FilePresetPersistence>("presets"));
            return instance;
        }

        // Delete copy and move operations
        PresetManager(const PresetManager&) = delete;
        PresetManager& operator=(const PresetManager&) = delete;
        PresetManager(PresetManager&&) = delete;
        PresetManager& operator=(PresetManager&&) = delete;

    private:
        // Helper struct to maintain sorted indices
        struct PresetIndex
        {
            int lastModified;
            size_t index;
            std::string name;

            bool operator<(const PresetIndex& other) const
            {
                // Sort by lastModified (descending) and then by name for stable sorting
                return lastModified > other.lastModified ||
                    (lastModified == other.lastModified && name < other.name);
            }
        };

    public:
        void initialize(std::unique_ptr<IPresetPersistence> persistence)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_persistence = std::move(persistence);
            m_currentPresetName = std::nullopt;
            m_currentPresetIndex = 0;
            LOG_DEBUG("[PresetManager::initialize] Initializing with custom persistence");
            loadPresetsAsync();
        }

        // Preset management methods
        std::future<bool> savePreset(const ModelPreset& preset)
        {
            LOG_DEBUG("[PresetManager::savePreset] Saving preset: " + preset.name);
            return std::async(std::launch::async, [this, preset]()
                { return savePresetInternal(preset); });
        }

        std::future<bool> saveCurrentPreset()
        {
            LOG_DEBUG("[PresetManager::saveCurrentPreset] Saving current preset");
            return std::async(std::launch::async, [this]()
                { return saveCurrentPresetInternal(); });
        }

        std::future<bool> saveCurrentPresetToPath(const std::filesystem::path& filePath)
        {
            LOG_DEBUG("[PresetManager::saveCurrentPresetToPath] Saving current preset to path: " + filePath.string());
            return std::async(std::launch::async, [this, filePath]()
                { return saveCurrentPresetToPathInternal(filePath); });
        }

        std::future<bool> deletePreset(const std::string& presetName)
        {
            LOG_DEBUG("[PresetManager::deletePreset] Deleting preset: " + presetName);
            return std::async(std::launch::async, [this, presetName]()
                { return deletePresetInternal(presetName); });
        }

        // New method: Copy current preset as a new preset with a new name
        std::future<bool> copyCurrentPresetAs(const std::string& newName)
        {
            LOG_DEBUG("[PresetManager::copyCurrentPresetAs] Copying current preset as: " + newName);
            return std::async(std::launch::async, [this, newName]()
                { return copyCurrentPresetAsInternal(newName); });
        }

        std::vector<ModelPreset> getPresets() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            std::vector<ModelPreset> sortedPresets;
            sortedPresets.reserve(m_presets.size());

            // Use the sorted indices to return presets in order
            for (const auto& idx : m_sortedIndices)
            {
                sortedPresets.push_back(m_presets[idx.index]);
            }
            return sortedPresets;
        }

        std::optional<std::reference_wrapper<ModelPreset>> getCurrentPreset()
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentPresetName || m_currentPresetIndex >= m_presets.size())
            {
                return std::nullopt;
            }
            return m_presets[m_currentPresetIndex];
        }

        bool switchPreset(const std::string& presetName)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_presetNameToIndex.find(presetName);
            if (it == m_presetNameToIndex.end())
            {
                LOG_WARNING("[PresetManager::switchPreset] Preset not found: " + presetName);
                return false;
            }

            m_currentPresetName = presetName;
            m_currentPresetIndex = it->second;

            LOG_DEBUG("[PresetManager::switchPreset] Switched to preset: " + presetName);

			// Set the last modified time to the current time
            {
				// unlock the mutex before saving the preset
				lock.unlock();
                saveCurrentPresetInternal();
            }

            return true;
        }

        bool hasUnsavedChanges() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentPresetName)
                return false;

            size_t index = m_currentPresetIndex;
            if (index >= m_presets.size())
                return false;

            const auto& current = m_presets[index];
            const auto& original = m_originalPresets[index];

            return current != original;
        }

        void resetCurrentPreset()
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentPresetName)
                return;

            size_t index = m_currentPresetIndex;
            if (index >= m_originalPresets.size())
                return;

            LOG_DEBUG("[PresetManager::resetCurrentPreset] Resetting current preset: " + m_presets[index].name);
            m_presets[index] = m_originalPresets[index];
        }

        size_t getSortedPresetIndex(const std::string& presetName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            size_t sortedIndex = 0;
            for (const auto& idx : m_sortedIndices)
            {
                if (idx.name == presetName)
                {
                    return sortedIndex;
                }
                sortedIndex++;
            }
            return 0; // Not found
        }

        std::optional<ModelPreset> getPresetByTimestamp(int timestamp) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = std::find_if(m_sortedIndices.begin(), m_sortedIndices.end(),
                [timestamp](const PresetIndex& idx)
                { return idx.lastModified == timestamp; });

            if (it != m_sortedIndices.end())
            {
                return m_presets[it->index];
            }
            return std::nullopt;
        }

    private:
        explicit PresetManager(std::unique_ptr<IPresetPersistence> persistence)
            : m_persistence(std::move(persistence)),
            m_currentPresetName(std::nullopt),
            m_currentPresetIndex(0)
        {
            LOG_DEBUG("[PresetManager::PresetManager] Constructing PresetManager");
            loadPresetsAsync();
        }

        void loadPresetsAsync()
        {
            LOG_DEBUG("[PresetManager::loadPresetsAsync] Loading presets asynchronously");
            std::async(std::launch::async, [this]()
                {
                    auto presets = m_persistence->loadAllPresets().get();
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_presets = std::move(presets);
                    m_originalPresets = m_presets;

                    m_presetNameToIndex.clear();
                    m_sortedIndices.clear();

                    for (size_t i = 0; i < m_presets.size(); ++i)
                    {
                        m_presetNameToIndex[m_presets[i].name] = i;
                        m_sortedIndices.insert({ m_presets[i].lastModified, i, m_presets[i].name });
                    }

                    if (!m_presets.empty())
                    {
                        // Select the most recent preset
                        auto mostRecent = m_sortedIndices.begin();
                        m_currentPresetIndex = mostRecent->index;
                        m_currentPresetName = mostRecent->name;
                        LOG_INFO("[PresetManager::loadPresetsAsync] Loaded " + std::to_string(m_presets.size()) + " presets, most recent: " + mostRecent->name);
                    }
                    else
                    {
                        LOG_WARNING("[PresetManager::loadPresetsAsync] No presets found, creating default");
                        createDefaultPreset();
                    } });
        }

        void createDefaultPreset()
        {
            LOG_DEBUG("[PresetManager::createDefaultPreset] Creating default preset");
            const int currentTime = static_cast<int>(std::time(nullptr));
            ModelPreset defaultPreset{
                0,
                currentTime,
                "default",
                "You are a helpful assistant.",
                0.7f,
                0.9f,
                50.0f,
                42,
                0.0f,
                0.0f };

            size_t newIndex = m_presets.size();
            m_presets.push_back(defaultPreset);
            m_originalPresets.push_back(defaultPreset);
            m_presetNameToIndex[defaultPreset.name] = newIndex;
            m_sortedIndices.insert({ currentTime, newIndex, defaultPreset.name });

            m_currentPresetName = defaultPreset.name;
            m_currentPresetIndex = newIndex;

            m_persistence->savePreset(defaultPreset);
        }

        bool savePresetInternal(const ModelPreset& preset)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            // Validate preset name
            if (!isValidPresetName(preset.name))
            {
                LOG_ERROR("[PresetManager::savePresetInternal] Invalid preset name: " + preset.name);
                return false;
            }

            size_t index;
            bool isNewPreset = false;
            auto it = m_presetNameToIndex.find(preset.name);
            if (it != m_presetNameToIndex.end())
            {
                index = it->second;
                // Remove old index from sorted indices
                auto oldTimestamp = m_presets[index].lastModified;
                m_sortedIndices.erase({ oldTimestamp, index, preset.name });
            }
            else
            {
                // New preset
                index = m_presets.size();
                m_presets.push_back(preset);
                m_originalPresets.push_back(preset);
                m_presetNameToIndex[preset.name] = index;
                isNewPreset = true;
            }

            // Update timestamp
            int newTimestamp = static_cast<int>(std::time(nullptr));
            m_presets[index] = preset;
            m_presets[index].lastModified = newTimestamp;
            m_originalPresets[index] = m_presets[index];

            // Add new index to sorted indices
            m_sortedIndices.insert({ newTimestamp, index, preset.name });

            // Save to persistence
            bool result = m_persistence->savePreset(m_presets[index]).get();

            if (result) {
                LOG_INFO("[PresetManager::savePresetInternal] Preset saved: " + preset.name);
            } else {
                LOG_ERROR("[PresetManager::savePresetInternal] Failed to save preset: " + preset.name);
            }

            return result;
        }

        bool saveCurrentPresetInternal()
        {
            if (!m_currentPresetName || m_currentPresetIndex >= m_presets.size())
            {
                LOG_WARNING("[PresetManager::saveCurrentPresetInternal] No current preset to save");
                return false;
            }
            ModelPreset& currentPreset = m_presets[m_currentPresetIndex];
            return savePresetInternal(currentPreset);
        }

        bool saveCurrentPresetToPathInternal(const std::filesystem::path& filePath)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_currentPresetName || m_currentPresetIndex >= m_presets.size())
            {
                // No current preset to save
                LOG_WARNING("[PresetManager::saveCurrentPresetToPathInternal] No current preset to save to path");
                return false;
            }

            const ModelPreset& currentPreset = m_presets[m_currentPresetIndex];

            // Use the persistence strategy to save the preset to the given path
            bool result = m_persistence->savePresetToPath(currentPreset, filePath).get();

            if (result) {
                LOG_INFO("[PresetManager::saveCurrentPresetToPathInternal] Preset saved to path: " + filePath.string());
            } else {
                LOG_ERROR("[PresetManager::saveCurrentPresetToPathInternal] Failed to save preset to path: " + filePath.string());
            }

            // We might not need to update internal structures since we're just saving a copy

            return result;
        }

        bool deletePresetInternal(const std::string& presetName)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            auto it = m_presetNameToIndex.find(presetName);
            if (it != m_presetNameToIndex.end())
            {
                size_t indexToRemove = it->second;

                // Remove from sorted indices
                auto timestamp = m_presets[indexToRemove].lastModified;
                m_sortedIndices.erase({ timestamp, indexToRemove, presetName });

                m_presets.erase(m_presets.begin() + indexToRemove);
                m_originalPresets.erase(m_originalPresets.begin() + indexToRemove);
                m_presetNameToIndex.erase(it);

                // Update indices
                updateIndicesAfterDeletion(indexToRemove);

                if (m_currentPresetIndex == indexToRemove)
                {
                    m_currentPresetName = std::nullopt;
                    m_currentPresetIndex = 0;
                }
                else if (m_currentPresetIndex > indexToRemove)
                {
                    m_currentPresetIndex--;
                }

                // Delete from persistence
                bool result = m_persistence->deletePreset(presetName).get();

                if (result) {
                    LOG_INFO("[PresetManager::deletePresetInternal] Preset deleted: " + presetName);
                } else {
                    LOG_ERROR("[PresetManager::deletePresetInternal] Failed to delete preset: " + presetName);
                }

                return result;
            }

            LOG_WARNING("[PresetManager::deletePresetInternal] Preset not found: " + presetName);
            return false;
        }

        bool copyCurrentPresetAsInternal(const std::string& newName)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            // Validate new name
            if (!isValidPresetName(newName))
            {
                LOG_ERROR("[PresetManager::copyCurrentPresetAsInternal] Invalid new preset name: " + newName);
                return false;
            }

            // Check if newName already exists
            if (m_presetNameToIndex.find(newName) != m_presetNameToIndex.end())
            {
                // Name already exists
                LOG_WARNING("[PresetManager::copyCurrentPresetAsInternal] Preset name already exists: " + newName);
                return false;
            }

            // Check if there's a current preset to copy
            if (!m_currentPresetName || m_currentPresetIndex >= m_presets.size())
            {
                LOG_WARNING("[PresetManager::copyCurrentPresetAsInternal] No current preset to copy");
                return false;
            }

            // Create a copy of the current preset
            ModelPreset newPreset = m_presets[m_currentPresetIndex];
            newPreset.name = newName;
            newPreset.lastModified = static_cast<int>(std::time(nullptr));

            // Add new preset to data structures
            size_t newIndex = m_presets.size();
            m_presets.push_back(newPreset);
            m_originalPresets.push_back(newPreset);
            m_presetNameToIndex[newName] = newIndex;
            m_sortedIndices.insert({ newPreset.lastModified, newIndex, newName });

            // Save to persistence
            bool result = m_persistence->savePreset(newPreset).get();

            if (!result)
            {
                // Rollback changes if save failed
                m_presets.pop_back();
                m_originalPresets.pop_back();
                m_presetNameToIndex.erase(newName);
                m_sortedIndices.erase({ newPreset.lastModified, newIndex, newName });
                LOG_ERROR("[PresetManager::copyCurrentPresetAsInternal] Failed to save copied preset: " + newName);
            } else {
                LOG_INFO("[PresetManager::copyCurrentPresetAsInternal] Copied current preset as: " + newName);
            }

            return result;
        }

        void updateIndicesAfterDeletion(size_t deletedIndex)
        {
            // Update presetNameToIndex
            for (auto& pair : m_presetNameToIndex)
            {
                if (pair.second > deletedIndex)
                {
                    pair.second--;
                }
            }

            // Update sortedIndices
            std::set<PresetIndex> newSortedIndices;
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

        // Validation helpers
        bool isValidPresetName(const std::string& name) const
        {
            if (name.empty() || name.length() > 256)
                return false;
            const std::string invalidChars = R"(<>:"/\|?*)";
            return name.find_first_of(invalidChars) == std::string::npos;
        }

        // Member variables
        mutable std::shared_mutex m_mutex;
        std::unique_ptr<IPresetPersistence> m_persistence;
        std::vector<ModelPreset> m_presets;
        std::vector<ModelPreset> m_originalPresets;
        std::unordered_map<std::string, size_t> m_presetNameToIndex;
        std::set<PresetIndex> m_sortedIndices;
        std::optional<std::string> m_currentPresetName;
        size_t m_currentPresetIndex;
    };

	inline void initializePresetManager()
	{
        LOG_DEBUG("[PresetManager::initializePresetManager] Initializing PresetManager singleton");
		PresetManager::getInstance();
	}

	inline void initializePresetManagerWithCustomPersistence(std::unique_ptr<IPresetPersistence> persistence)
	{
        LOG_DEBUG("[PresetManager::initializePresetManagerWithCustomPersistence] Initializing PresetManager with custom persistence");
		PresetManager::getInstance().initialize(std::move(persistence));
	}

} // namespace Model