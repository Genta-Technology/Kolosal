#pragma once

#include "preset_manager.hpp"
#include "model_persistence.hpp"

#include <types.h>
#include <inference_interface.h>
#include <string>
#include <vector>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <future>
#include <iostream>
#include <curl/curl.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#endif

#pragma comment(lib, "wbemuuid.lib")

typedef IInferenceEngine* (CreateInferenceEngineFunc)();
typedef void (DestroyInferenceEngineFunc)(IInferenceEngine*);

namespace Model
{

    class ModelManager
    {
    public:
        static ModelManager &getInstance()
        {
            static ModelManager instance(std::make_unique<FileModelPersistence>("models"));
            return instance;
        }

        ModelManager(const ModelManager &) = delete;
        ModelManager &operator=(const ModelManager &) = delete;
        ModelManager(ModelManager &&) = delete;
        ModelManager &operator=(ModelManager &&) = delete;

        void initialize(std::unique_ptr<IModelPersistence> persistence)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_persistence = std::move(persistence);
            m_currentModelName = std::nullopt;
            m_currentModelIndex = 0;
        }

        bool unloadModel()
        {
			std::unique_lock<std::shared_mutex> lock(m_mutex);

            if (!m_modelLoaded)
            {
                return false;
            }

			if (!m_inferenceEngine)
			{
				return false;
			}

			m_unloadInProgress = true;

			lock.unlock();

			// Start async unloading process
			auto unloadFuture = unloadModelAsync();

			// Handle unload completion
            m_unloadFutures.emplace_back(std::async(std::launch::async,
                [this, unloadFuture = std::move(unloadFuture)]() mutable {
					if (unloadFuture.get())
					{
						std::cout << "[ModelManager] Successfully unloaded model\n";
					}
					else
					{
						std::cerr << "[ModelManager] Failed to unload model\n";
					}

                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        m_modelLoaded = false;
                        m_unloadInProgress = false;
                        resetModelState();
                    }
                }));
        }

        // Switch to a specific model variant. If not downloaded, trigger download.
        bool switchModel(const std::string& modelName, const std::string& variantType)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            auto it = m_modelNameToIndex.find(modelName);
            if (it == m_modelNameToIndex.end()) {
                return false; // Model not found
            }

            // Update state with the new model/variant
            m_currentModelName = modelName;
            m_currentVariantType = variantType;
            m_currentModelIndex = it->second;
            setPreferredVariant(modelName, variantType);

            // Get the desired variant
            ModelVariant* variant = getVariantLocked(m_currentModelIndex, m_currentVariantType);
            if (!variant) {
                return false;
            }

            if (!variant->isDownloaded && variant->downloadProgress == 0.0) {
                startDownloadAsyncLocked(m_currentModelIndex, m_currentVariantType);
                return true;
            }

            // Handle already downloaded case
            variant->lastSelected = static_cast<int>(std::time(nullptr));
            m_persistence->saveModelData(m_models[m_currentModelIndex]);

            // Prevent concurrent model loading
            if (m_loadInProgress) {
                std::cerr << "[ModelManager] Already loading a model, cannot switch now\n";
                return false;
            }

            m_loadInProgress = true;
            
            // Release lock before async operations
            lock.unlock();

            // Start async loading process
            auto loadFuture = loadModelIntoEngineAsync();
            
            // Handle load completion
            m_loadFutures.emplace_back(std::async(std::launch::async,
                [this, loadFuture = std::move(loadFuture)]() mutable {
                    bool success = false;
                    try {
                        success = loadFuture.get();
                    }
                    catch (const std::exception& e) {
                        std::cerr << "[ModelManager] Model load error: " << e.what() << "\n";
                    }

                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        m_loadInProgress = false;

                        if (success) {
                            m_modelLoaded = true;
                            std::cout << "[ModelManager] Successfully switched models\n";
                        }
                        else {
                            resetModelState();
                            std::cerr << "[ModelManager] Failed to load model\n";
                        }
                    }

                    // Cleanup completed futures
                    m_loadFutures.erase(
                        std::remove_if(m_loadFutures.begin(), m_loadFutures.end(),
                            [](const std::future<void>& f) {
                                return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                            }),
                        m_loadFutures.end()
                    );
                }));

            return true;
        }

        bool downloadModel(size_t modelIndex, const std::string &variantType)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (modelIndex >= m_models.size())
            {
                return false; // Invalid index
            }

            ModelVariant *variant = getVariantLocked(modelIndex, variantType);
            if (!variant)
                return false;

            // If already downloaded or currently downloading (progress > 0 but not finished), do nothing
            if (variant->isDownloaded || variant->downloadProgress > 0.0)
            {
                return false;
            }

            // Start new download
            startDownloadAsyncLocked(modelIndex, variantType);
            return true;
        }

        bool isModelDownloaded(size_t modelIndex, const std::string &variantType) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (modelIndex >= m_models.size())
                return false;

            const ModelVariant *variant = getVariantLocked(modelIndex, variantType);
            return variant ? variant->isDownloaded : false;
        }

        double getModelDownloadProgress(size_t modelIndex, const std::string &variantType) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (modelIndex >= m_models.size())
                return 0.0;

            const ModelVariant *variant = getVariantLocked(modelIndex, variantType);
            return variant ? variant->downloadProgress : 0.0;
        }

        std::vector<ModelData> getModels() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_models;
        }

        std::optional<std::string> getCurrentModelName() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_currentModelName;
        }

        std::string getCurrentVariantType() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_currentVariantType;
        }

        double getCurrentVariantProgress() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            const ModelVariant *variant = getVariantLocked(m_currentModelIndex, m_currentVariantType);
            return variant ? variant->downloadProgress : 0.0;
        }

        //--------------------------------------------------------------------------------------------
		// Inference Engine
		//--------------------------------------------------------------------------------------------

        ChatCompletionParameters buildChatCompletionParameters(
            const Chat::ChatHistory& currentChat,
            const std::string& userInput
        ) {
            ChatCompletionParameters completionParams;
            auto& presetManager = Model::PresetManager::getInstance();
            auto& modelManager = Model::ModelManager::getInstance();
            auto& chatManager = Chat::ChatManager::getInstance();

            auto currentPresetOpt = presetManager.getCurrentPreset();
            if (!currentPresetOpt.has_value()) {
                std::cerr << "[ChatSection] No preset available. Using default values.\n";
            }
            const auto& currentPreset = currentPresetOpt.value().get();

            // Add the system prompt as the first message.
            completionParams.messages.push_back({ "system", currentPreset.systemPrompt.c_str() });

            // Append all previous messages.
            for (const auto& msg : currentChat.messages) {
                completionParams.messages.push_back({ msg.role.c_str(), msg.content.c_str() });
            }

            // Append the new user message.
            completionParams.messages.push_back({ "user", userInput.c_str() });

            // Copy over additional parameters.
            completionParams.randomSeed = currentPreset.random_seed;
            completionParams.maxNewTokens = static_cast<int>(currentPreset.max_new_tokens);
            completionParams.minLength = static_cast<int>(currentPreset.min_length);
            completionParams.temperature = currentPreset.temperature;
            completionParams.topP = currentPreset.top_p;
            completionParams.streaming = true;

            // Set the kvCacheFilePath using the current model and variant.
            auto kvCachePathOpt = chatManager.getCurrentKvChatPath(
                modelManager.getCurrentModelName().value(),
                modelManager.getCurrentVariantType()
            );
            if (kvCachePathOpt.has_value()) {
                completionParams.kvCacheFilePath = kvCachePathOpt.value().string();
            }

            return completionParams;
        }

        ChatCompletionParameters buildChatCompletionParameters(
            const Chat::ChatHistory& currentChat
        ) {
            ChatCompletionParameters completionParams;
            auto& presetManager = Model::PresetManager::getInstance();
            auto& modelManager = Model::ModelManager::getInstance();
            auto& chatManager = Chat::ChatManager::getInstance();

            auto currentPresetOpt = presetManager.getCurrentPreset();
            if (!currentPresetOpt.has_value()) {
                std::cerr << "[ChatSection] No preset available. Using default values.\n";
            }
            const auto& currentPreset = currentPresetOpt.value().get();

            // Add the system prompt as the first message.
            completionParams.messages.push_back({ "system", currentPreset.systemPrompt.c_str() });

            // Append all previous messages.
            for (const auto& msg : currentChat.messages) {
                completionParams.messages.push_back({ msg.role.c_str(), msg.content.c_str() });
            }

            // Copy over additional parameters.
            completionParams.randomSeed = currentPreset.random_seed;
            completionParams.maxNewTokens = static_cast<int>(currentPreset.max_new_tokens);
            completionParams.minLength = static_cast<int>(currentPreset.min_length);
            completionParams.temperature = currentPreset.temperature;
            completionParams.topP = currentPreset.top_p;
            completionParams.streaming = true;

            // Set the kvCacheFilePath using the current model and variant.
            auto kvCachePathOpt = chatManager.getCurrentKvChatPath(
                modelManager.getCurrentModelName().value(),
                modelManager.getCurrentVariantType()
            );
            if (kvCachePathOpt.has_value()) {
                completionParams.kvCacheFilePath = kvCachePathOpt.value().string();
            }

            return completionParams;
        }

        void setStreamingCallback(std::function<void(const std::string&, const float, const int)> callback)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_streamingCallback = std::move(callback);
        }

        bool stopJob(int jobId)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return false;
            }
            m_inferenceEngine->stopJob(jobId);
            return true;
        }

        int startCompletionJob(const CompletionParameters& params)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngine)
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    return -1;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    return -1;
                }
            }

            int jobId = m_inferenceEngine->submitCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit completions job.\n";
                return -1;
            }

            m_jobIds.push_back(jobId);

            std::thread([this, jobId]() {
                // Poll while job is running or until the engine says it's done
				this->setModelGenerationInProgress(true);
                while (true)
                {
                    if (this->m_inferenceEngine->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngine->getJobResult(jobId);

                    if (!partial.text.empty()) {
                        // Call the user�s callback
                        // (hold shared lock if needed to be thread-safe)
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        if (m_streamingCallback) {
                            m_streamingCallback(partial.text, partial.tps, jobId);
                        }
                    }

                    if (this->m_inferenceEngine->isJobFinished(jobId)) break;

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

				this->setModelGenerationInProgress(false);

                {
                    // remove job id from m_jobIds
					m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
                }

                // Reset jobid tracking on chat manager to -1
                {
                    if (!Chat::ChatManager::getInstance().removeJobId(jobId))
                    {
						std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                    }
                }
                }).detach();

            return jobId;
        }

        int startChatCompletionJob(const ChatCompletionParameters& params)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngine)
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    return -1;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    return -1;
                }
            }

            int jobId = m_inferenceEngine->submitChatCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit chat completions job.\n";
                return -1;
            }

            m_jobIds.push_back(jobId);

            std::thread([this, jobId]() {
                // Poll while job is running or until the engine says it's done
				this->setModelGenerationInProgress(true);
                auto& chatManager = Chat::ChatManager::getInstance();

                while (true)
                {
                    if (this->m_inferenceEngine->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngine->getJobResult(jobId);

                    if (!partial.text.empty()) {
                        // Call the user�s callback
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        if (m_streamingCallback) {
                            m_streamingCallback(partial.text, partial.tps, jobId);
                        }
                    }

                    if (this->m_inferenceEngine->isJobFinished(jobId)) break;

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

				this->setModelGenerationInProgress(false);

				{
					// remove job id from m_jobIds
					m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
				}

				// save the chat history
				{
                    auto chatName = chatManager.getChatNameByJobId(jobId);
                    if (!chatManager.saveChat(chatName))
                    {
                        std::cerr << "[ModelManager] Failed to save chat: " << chatName << std::endl;
                    }
				}

                // Reset jobid tracking on chat manager to -1
                {
                    if (!chatManager.removeJobId(jobId))
                    {
                        std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                    }
                }
                }).detach();

            return jobId;
        }

        bool isJobFinished(int jobId)
        {
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true; // No engine means nothing is running
            }
            return m_inferenceEngine->isJobFinished(jobId);
        }

        CompletionResult getJobResult(int jobId)
        {
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return { {}, "" };
            }
			return m_inferenceEngine->getJobResult(jobId);
        }

        bool hasJobError(int jobId)
        {
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true;
            }
			return m_inferenceEngine->hasJobError(jobId);
        }

        std::string getJobError(int jobId)
        {
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return "Inference engine not initialized";
            }
            return m_inferenceEngine->getJobError(jobId);
        }

        std::string getCurrentVariantForModel(const std::string& modelName) const 
        {
            auto it = m_modelVariantMap.find(modelName);
            return it != m_modelVariantMap.end() ? it->second : "8-bit Quantized";
        }

        void setPreferredVariant(const std::string& modelName, const std::string& variantType)
        {
            m_modelVariantMap[modelName] = variantType;
        }

        bool cancelDownload(size_t modelIndex, const std::string& variantType)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (modelIndex >= m_models.size())
                return false;
            ModelVariant* variant = getVariantLocked(modelIndex, variantType);
            if (!variant)
                return false;
            variant->cancelDownload = true;
            return true;
        }

        bool deleteDownloadedModel(size_t modelIndex, const std::string& variantType)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (modelIndex >= m_models.size())
                return false;

            ModelVariant* variant = getVariantLocked(modelIndex, variantType);
            if (!variant)
                return false;

            lock.unlock();

            if (modelIndex == m_currentModelIndex && variantType == m_currentVariantType)
            {
				unloadModel();
            }

            // Call the persistence layer to delete the file.
            auto fut = m_persistence->deleteModelVariant(m_models[modelIndex], *variant);
            fut.get(); // Wait for deletion to complete.
            return true;
        }

        void resetModelState() {
            m_currentModelName = std::nullopt;
            m_currentVariantType = "";
            m_currentModelIndex = 0;
            m_modelLoaded = false;
        }

		bool isCurrentlyGenerating() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_modelGenerationInProgress;
		}

		bool setModelGenerationInProgress(bool inProgress)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			m_modelGenerationInProgress = inProgress;
			return true;
		}

		bool isModelLoaded() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_modelLoaded;
		}

		bool isLoadInProgress() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_loadInProgress;
		}

		bool isUnloadInProgress() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_unloadInProgress;
		}

    private:
        explicit ModelManager(std::unique_ptr<IModelPersistence> persistence)
            : m_persistence(std::move(persistence))
            , m_currentModelName(std::nullopt)
            , m_currentModelIndex(0)
            , m_inferenceLibHandle(nullptr)
            , m_createInferenceEnginePtr(nullptr)
			, m_inferenceEngine(nullptr)
			, m_modelLoaded(false)
            , m_modelGenerationInProgress(false)
        {
            startAsyncInitialization();
        }

        ~ModelManager()
        {
            stopAllJobs();
            cancelAllDownloads();

            if (m_initializationFuture.valid()) {
                m_initializationFuture.wait();
            }

            if (m_inferenceEngine && m_destroyInferenceEnginePtr) {
                m_destroyInferenceEnginePtr(m_inferenceEngine);
                m_inferenceEngine = nullptr;
            }

            if (m_inferenceLibHandle) {
#ifdef _WIN32
                FreeLibrary(m_inferenceLibHandle);
#endif
                m_inferenceLibHandle = nullptr;
            }

            // Wait for any remaining downloads
            for (auto& future : m_downloadFutures) {
                if (future.valid()) {
                    future.wait();
                }
            }

            // Wait for any remaining loads
            for (auto& future : m_loadFutures) {
                if (future.valid()) {
                    future.wait();
                }
            }
        }

        void startAsyncInitialization() {
            m_initializationFuture = std::async(std::launch::async, [this]() {
#ifdef DEBUG
                std::cout << "[ModelManager] Initializing model manager" << std::endl;
#endif

                // Run model loading and Vulkan check in parallel
                auto modelsFuture = std::async(std::launch::async, &ModelManager::loadModels, this);
                auto vulkanFuture = std::async(std::launch::async, &ModelManager::useVulkanBackend, this);

                modelsFuture.get();
                bool useVulkan = vulkanFuture.get();

                std::string backendName = "InferenceEngineLib.dll";
                if (useVulkan) {
                    backendName = "InferenceEngineLibVulkan.dll";
                }

#ifdef DEBUG
                std::cout << "[ModelManager] Using backend: " << backendName << std::endl;
#endif

                if (!loadInferenceEngineDynamically(backendName.c_str())) {
                    std::cerr << "[ModelManager] Failed to load inference engine for backend: "
                        << backendName << std::endl;
                    return;
                }

                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_loadInProgress = true;
                }

                // Start async model loading
                auto modelLoadFuture = loadModelIntoEngineAsync();
                if (!modelLoadFuture.get()) { // Wait for async load to complete
                    resetModelState();
                }

                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_loadInProgress = false;
                }
                });
        }

        void loadModels()
        {
            // Load all models from persistence.
            auto loadedModels = m_persistence->loadAllModels().get();

            // Merge any duplicate models by name.
            // (If duplicate files exist, we merge by choosing the variant with the higher lastSelected value.)
            std::unordered_map<std::string, ModelData> mergedModels;
            for (auto& model : loadedModels)
            {
                auto it = mergedModels.find(model.name);
                if (it == mergedModels.end())
                {
                    mergedModels[model.name] = model;
                }
                else
                {
                    // For each variant, update if the new one was used more recently.
                    if (model.fullPrecision.lastSelected > it->second.fullPrecision.lastSelected)
                        it->second.fullPrecision = model.fullPrecision;
                    if (model.quantized8Bit.lastSelected > it->second.quantized8Bit.lastSelected)
                        it->second.quantized8Bit = model.quantized8Bit;
                    if (model.quantized4Bit.lastSelected > it->second.quantized4Bit.lastSelected)
                        it->second.quantized4Bit = model.quantized4Bit;
                }
            }

            // Rebuild the models vector.
            std::vector<ModelData> models;
            for (auto& pair : mergedModels)
            {
                models.push_back(pair.second);
            }

            // Check and fix each variant�s download status.
            for (auto& model : models)
            {
                checkAndFixDownloadStatus(model.fullPrecision);
                checkAndFixDownloadStatus(model.quantized8Bit);
                checkAndFixDownloadStatus(model.quantized4Bit);
            }

            // Update internal state (including the variant map) under lock.
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_models = std::move(models);
                m_modelNameToIndex.clear();
                m_modelVariantMap.clear();

                // For each model, choose the "best" variant based on lastSelected,
                // but prioritize the downloaded state and the desired order:
                // first check 8-bit Quantized, then 4-bit Quantized, and lastly Full Precision.
                for (size_t i = 0; i < m_models.size(); ++i)
                {
                    m_modelNameToIndex[m_models[i].name] = i;
                    int bestEffectiveValue = -1;
                    std::string bestVariant;

                    // Helper lambda to calculate effective value.
                    auto checkVariant = [&](const ModelVariant& variant) {
                        int effectiveValue = variant.lastSelected;
                        if (variant.isDownloaded)
                        {
                            effectiveValue += 1000000;  // boost for downloaded variants
                        }
                        if (effectiveValue > bestEffectiveValue)
                        {
                            bestEffectiveValue = effectiveValue;
                            bestVariant = variant.type;
                        }
                        };

                    // Check in the desired order: 8-bit, then 4-bit, then full precision.
                    checkVariant(m_models[i].quantized8Bit);
                    checkVariant(m_models[i].quantized4Bit);
                    checkVariant(m_models[i].fullPrecision);

                    // If no variant was ever selected, default to "8-bit Quantized".
                    if (bestVariant.empty())
                    {
                        bestVariant = "8-bit Quantized";
                    }
                    m_modelVariantMap[m_models[i].name] = bestVariant;
                }
            }

            // Determine the overall current model selection (for loading into the engine).
            int maxLastSelected = -1;
            size_t selectedModelIndex = 0;
            std::string selectedVariantType;
            for (size_t i = 0; i < m_models.size(); ++i)
            {
                const auto& model = m_models[i];
                // Order is arbitrary; adjust as needed.
                const ModelVariant* variants[] = { &model.quantized8Bit, &model.quantized4Bit, &model.fullPrecision };
                for (const ModelVariant* variant : variants)
                {
                    if (variant->isDownloaded && variant->lastSelected > maxLastSelected)
                    {
                        maxLastSelected = variant->lastSelected;
                        selectedModelIndex = i;
                        selectedVariantType = variant->type;
                    }
                }
            }
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                if (maxLastSelected >= 0)
                {
                    m_currentModelName = m_models[selectedModelIndex].name;
                    m_currentModelIndex = selectedModelIndex;
                    m_currentVariantType = selectedVariantType;
                }
                else
                {
                    m_currentModelName = std::nullopt;
                    m_currentVariantType.clear();
                    m_currentModelIndex = 0;
                }
            }
        }

        void checkAndFixDownloadStatus(ModelVariant& variant) 
        {
            if (variant.isDownloaded) 
            {
                // Check if file exists
                if (!std::filesystem::exists(variant.path)) 
                {
                    // File doesn't exist, reset
                    variant.isDownloaded = false;
                    variant.downloadProgress = 0.0;
                }

                return;
            }
            
            // if variant is not downloaded, but file exists, set isDownloaded to true
            if (std::filesystem::exists(variant.path)) 
            {
                variant.isDownloaded = true;
                variant.downloadProgress = 100.0;
            }
        }

        ModelVariant *getVariantLocked(size_t modelIndex, const std::string &variantType) const
        {
            if (modelIndex >= m_models.size())
                return nullptr;

            const auto &model = m_models[modelIndex];
            if (variantType == "Full Precision")
            {
                return const_cast<ModelVariant *>(&model.fullPrecision);
            }
			else if (variantType == "8-bit Quantized")
			{
				return const_cast<ModelVariant*>(&model.quantized8Bit);
			}
			else if (variantType == "4-bit Quantized")
            {
                return const_cast<ModelVariant *>(&model.quantized4Bit);
            }
            else
			{
				return nullptr;
			}
        }

        void startDownloadAsyncLocked(size_t modelIndex, const std::string &variantType)
        {
            if (modelIndex >= m_models.size())
                return;

            ModelVariant* variant = getVariantLocked(modelIndex, variantType);
            if (!variant)
                return;

            ModelData* model = &m_models[modelIndex];

            variant->downloadProgress = 0.01f;  // 0% looks like no progress

            // Begin the asynchronous download.
            auto downloadFuture = m_persistence->downloadModelVariant(*model, *variant);

            // Chain a continuation that waits for the download to complete.
            m_downloadFutures.emplace_back(std::async(std::launch::async,
                [this, modelIndex, variantType, fut = std::move(downloadFuture)]() mutable {
                    // Wait for the download to finish.
                    fut.wait();

                    // After download, check if this model variant is still the current selection.
                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        if (m_currentModelIndex == modelIndex && m_currentVariantType == variantType)
                        {
                            // Unlock before loading the model.
                            lock.unlock();

                            auto loadFuture = loadModelIntoEngineAsync();
                            if (!loadFuture.get())
                            {
                                std::unique_lock<std::shared_mutex> restoreLock(m_mutex);
                                resetModelState();

                                std::cerr << "[ModelManager] Failed to load model after download completion.\n";
                            }
                        }
                    }
                }
            ));

            // Add cleanup after adding new future
            m_downloadFutures.erase(
                std::remove_if(m_downloadFutures.begin(), m_downloadFutures.end(),
                    [](auto& future) {
                        return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                    }),
                m_downloadFutures.end()
            );
        }

        bool useVulkanBackend() const
        {
            bool useVulkan = false;

            // Initialize COM
            HRESULT hres = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hres))
            {
                std::cerr << "[Error] Failed to initialize COM library. HR = 0x"
                    << std::hex << hres << std::endl;
                return useVulkan;
            }

            // Set COM security levels
            hres = CoInitializeSecurity(
                nullptr,
                -1,
                nullptr,
                nullptr,
                RPC_C_AUTHN_LEVEL_DEFAULT,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                nullptr,
                EOAC_NONE,
                nullptr
            );

            if (FAILED(hres) && hres != RPC_E_TOO_LATE) // Ignore if security is already initialized
            {
                std::cerr << "[Error] Failed to initialize security. HR = 0x"
                    << std::hex << hres << std::endl;
                CoUninitialize();
                return useVulkan;
            }

            // Obtain the initial locator to WMI
            IWbemLocator* pLoc = nullptr;
            hres = CoCreateInstance(
                CLSID_WbemLocator,
                0,
                CLSCTX_INPROC_SERVER,
                IID_IWbemLocator,
                reinterpret_cast<LPVOID*>(&pLoc)
            );

            if (FAILED(hres))
            {
                std::cerr << "[Error] Failed to create IWbemLocator object. HR = 0x"
                    << std::hex << hres << std::endl;
                CoUninitialize();
                return useVulkan;
            }

            // Connect to the ROOT\CIMV2 namespace
            IWbemServices* pSvc = nullptr;
            hres = pLoc->ConnectServer(
                _bstr_t(L"ROOT\\CIMV2"),
                nullptr,
                nullptr,
                nullptr,
                0,
                nullptr,
                nullptr,
                &pSvc
            );

            if (FAILED(hres))
            {
                std::cerr << "[Error] Could not connect to WMI. HR = 0x"
                    << std::hex << hres << std::endl;
                pLoc->Release();
                CoUninitialize();
                return useVulkan;
            }

            // Set security levels on the WMI proxy
            hres = CoSetProxyBlanket(
                pSvc,
                RPC_C_AUTHN_WINNT,
                RPC_C_AUTHZ_NONE,
                nullptr,
                RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                nullptr,
                EOAC_NONE
            );

            if (FAILED(hres))
            {
                std::cerr << "[Error] Could not set proxy blanket. HR = 0x"
                    << std::hex << hres << std::endl;
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return useVulkan;
            }

            // Query for all video controllers
            IEnumWbemClassObject* pEnumerator = nullptr;
            hres = pSvc->ExecQuery(
                bstr_t("WQL"),
                bstr_t("SELECT * FROM Win32_VideoController WHERE VideoProcessor IS NOT NULL"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                &pEnumerator
            );

            if (FAILED(hres))
            {
                std::cerr << "[Error] WMI query for Win32_VideoController failed. HR = 0x"
                    << std::hex << hres << std::endl;
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return useVulkan;
            }

            // Enumerate the results
            IWbemClassObject* pclsObj = nullptr;
            ULONG uReturn = 0;

            while (pEnumerator)
            {
                HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn)
                {
                    break;
                }

                // Check multiple properties to improve detection reliability
                VARIANT vtName, vtDesc, vtProcName;
                bool isGPUFound = false;

                // Check Name property
                hr = pclsObj->Get(L"Name", 0, &vtName, 0, 0);
                if (SUCCEEDED(hr) && vtName.vt == VT_BSTR && vtName.bstrVal != nullptr)
                {
                    std::wstring name = vtName.bstrVal;
                    if (name.find(L"NVIDIA") != std::wstring::npos ||
                        name.find(L"AMD") != std::wstring::npos ||
                        name.find(L"ATI") != std::wstring::npos ||
                        name.find(L"Radeon") != std::wstring::npos)
                    {
                        isGPUFound = true;
                    }
                }
                VariantClear(&vtName);

                // Check Description property if GPU not found yet
                if (!isGPUFound)
                {
                    hr = pclsObj->Get(L"Description", 0, &vtDesc, 0, 0);
                    if (SUCCEEDED(hr) && vtDesc.vt == VT_BSTR && vtDesc.bstrVal != nullptr)
                    {
                        std::wstring desc = vtDesc.bstrVal;
                        if (desc.find(L"NVIDIA") != std::wstring::npos ||
                            desc.find(L"AMD") != std::wstring::npos ||
                            desc.find(L"ATI") != std::wstring::npos ||
                            desc.find(L"Radeon") != std::wstring::npos)
                        {
                            isGPUFound = true;
                        }
                    }
                    VariantClear(&vtDesc);
                }

                // Check VideoProcessor property if GPU not found yet
                if (!isGPUFound)
                {
                    hr = pclsObj->Get(L"VideoProcessor", 0, &vtProcName, 0, 0);
                    if (SUCCEEDED(hr) && vtProcName.vt == VT_BSTR && vtProcName.bstrVal != nullptr)
                    {
                        std::wstring procName = vtProcName.bstrVal;
                        if (procName.find(L"NVIDIA")    != std::wstring::npos ||
                            procName.find(L"AMD")       != std::wstring::npos ||
                            procName.find(L"ATI")       != std::wstring::npos ||
                            procName.find(L"Radeon")    != std::wstring::npos)
                        {
                            isGPUFound = true;
                        }
                    }
                    VariantClear(&vtProcName);
                }

                if (isGPUFound)
                {
                    useVulkan = true;
                    pclsObj->Release();
                    break;
                }

                pclsObj->Release();
            }

            // Cleanup
            pEnumerator->Release();
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();

            return useVulkan;
        }

        bool loadInferenceEngineDynamically(const std::string& backendName)
        {
#ifdef _WIN32
            m_inferenceLibHandle = LoadLibraryA(backendName.c_str());
            if (!m_inferenceLibHandle) {
                std::cerr << "[ModelManager] Failed to load library: " << backendName
                    << ". Error code: " << GetLastError() << std::endl;
                return false;
            }

            // Retrieve the symbol
            m_createInferenceEnginePtr = (CreateInferenceEngineFunc*)
                GetProcAddress(m_inferenceLibHandle, "createInferenceEngine");
            if (!m_createInferenceEnginePtr) {
                std::cerr << "[ModelManager] Failed to get the address of createInferenceEngine from "
                    << backendName << std::endl;
                return false;
            }

            m_destroyInferenceEnginePtr = (DestroyInferenceEngineFunc*)
                GetProcAddress(m_inferenceLibHandle, "destroyInferenceEngine");

            if (!m_destroyInferenceEnginePtr) {
                std::cerr << "[ModelManager] Failed to get destroy function\n";
                FreeLibrary(m_inferenceLibHandle);
                return false;
            }

			std::cout << "[ModelManager] Successfully loaded inference engine from: "
				<< backendName << std::endl;

			m_inferenceEngine = m_createInferenceEnginePtr();
			if (!m_inferenceEngine) {
				std::cerr << "[ModelManager] Failed to get InferenceEngine instance from "
					<< backendName << std::endl;
				return false;
			}
#endif
            return true;
        }

        std::future<bool> ModelManager::loadModelIntoEngineAsync() {
            // Capture needed data under lock
            std::optional<std::string> modelDir;
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);

                if (!m_currentModelName) {
                    std::cerr << "[ModelManager] No model selected\n";
                    m_modelLoaded = false;
                    return std::async(std::launch::deferred, [] { return false; });
                }

                ModelVariant* variant = getVariantLocked(m_currentModelIndex, m_currentVariantType);
                if (!variant || !variant->isDownloaded || !std::filesystem::exists(variant->path)) {
                    m_modelLoaded = false;
                    return std::async(std::launch::deferred, [] { return false; });
                }

                modelDir = std::filesystem::absolute(
                    variant->path.substr(0, variant->path.find_last_of("/\\"))
                ).string();
            }

            // Launch heavy loading in async task
            return std::async(std::launch::async, [this, modelDir]() {
                try {
                    bool success = m_inferenceEngine->loadModel(modelDir->c_str());

                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        m_modelLoaded = success;
                    }

                    if (success) {
                        std::cout << "[ModelManager] Loaded model: " << *modelDir << "\n";
                    }
                    return success;
                }
                catch (const std::exception& e) {
                    std::cerr << "[ModelManager] Load failed: " << e.what() << "\n";
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_modelLoaded = false;
                    return false;
                }
                });
        }

        std::future<bool> ModelManager::unloadModelAsync() {
            // Capture current loaded state under lock
            bool isLoaded;
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                isLoaded = m_modelLoaded;

                if (!isLoaded) {
                    std::cerr << "[ModelManager] No model loaded to unload\n";
                    return std::async(std::launch::deferred, [] { return false; });
                }
            }

            // Launch heavy unloading in async task
            return std::async(std::launch::async, [this]() {
                try {
                    bool success = m_inferenceEngine->unloadModel();

                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        m_modelLoaded = !success; // False if unload succeeded, true otherwise
                    }

                    if (success) {
                        std::cout << "[ModelManager] Successfully unloaded model\n";
                    }
                    else {
                        std::cerr << "[ModelManager] Unload operation failed\n";
                    }
                    return success;
                }
                catch (const std::exception& e) {
                    std::cerr << "[ModelManager] Unload failed: " << e.what() << "\n";
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_modelLoaded = false; // Assume unloaded on exception
                    return false;
                }
                });
        }

        void stopAllJobs()
        {
            for (auto jobId : m_jobIds)
            {
                stopJob(jobId);
            }
        }

        void cancelAllDownloads() {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (auto& model : m_models) {
                // For each variant, if it�s still in progress (i.e. download progress is between 0 and 100)
                // set the cancel flag.
                if (model.fullPrecision.downloadProgress > 0.0 && model.fullPrecision.downloadProgress < 100.0) {
                    model.fullPrecision.cancelDownload = true;
                }
                if (model.quantized8Bit.downloadProgress > 0.0 && model.quantized8Bit.downloadProgress < 100.0) {
                    model.quantized8Bit.cancelDownload = true;
                }
                if (model.quantized4Bit.downloadProgress > 0.0 && model.quantized4Bit.downloadProgress < 100.0) {
                    model.quantized4Bit.cancelDownload = true;
                }
            }
        }

        mutable std::shared_mutex                       m_mutex;
        std::unique_ptr<IModelPersistence>              m_persistence;
        std::vector<ModelData>                          m_models;
        std::unordered_map<std::string, size_t>         m_modelNameToIndex;
        std::optional<std::string>                      m_currentModelName;
        std::string                                     m_currentVariantType;
        size_t                                          m_currentModelIndex;
        std::vector<std::future<void>>                  m_downloadFutures;
        std::future<bool>                               m_engineLoadFuture;
        std::future<void>                               m_initializationFuture;
        std::vector<std::future<void>>                  m_loadFutures;
        std::vector<std::future<void>>                  m_unloadFutures;
		std::atomic<bool>                               m_unloadInProgress{ false };
        std::atomic<bool>                               m_loadInProgress{ false };
        std::unordered_map<std::string, std::string>    m_modelVariantMap;
        std::atomic<bool>                               m_modelLoaded{ false };
		std::atomic<bool>                               m_modelGenerationInProgress{ false };
        std::vector<int>                                m_jobIds;

#ifdef _WIN32
        HMODULE m_inferenceLibHandle = nullptr;
#endif

        CreateInferenceEngineFunc*  m_createInferenceEnginePtr  = nullptr;
        DestroyInferenceEngineFunc* m_destroyInferenceEnginePtr = nullptr;

        IInferenceEngine* m_inferenceEngine = nullptr;

		std::function<void(const std::string&, const float, const int)> m_streamingCallback;
    };

    inline void initializeModelManager()
    {
        ModelManager::getInstance();
    }

    inline void initializeModelManagerWithCustomPersistence(std::unique_ptr<IModelPersistence> persistence)
    {
        ModelManager::getInstance().initialize(std::move(persistence));
    }

} // namespace Model