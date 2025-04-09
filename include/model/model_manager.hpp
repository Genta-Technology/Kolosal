#pragma once

#include "system_monitor.hpp"
#include "preset_manager.hpp"
#include "model_persistence.hpp"
#include "model_loader_config_manager.hpp"
#include "threadpool.hpp"

#include <kolosal_server.hpp>
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
    static std::atomic<int> seqCounter;

    // TODO: Instead of using singleton, i'm thinking of approaching it using a C style implementation
	//       to avoid the overhead of singleton pattern, and to make it more readable and maintainable.
    class ModelManager
    {
    public:
        static ModelManager &getInstance(const bool async = true)
        {
            static ModelManager instance(std::make_unique<FileModelPersistence>("models"), async);
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

        bool unloadModel(const std::string modelName)
        {
			std::unique_lock<std::shared_mutex> lock(m_mutex);

			if (!m_unloadInProgress.empty())
			{
				std::cerr << "[ModelManager] Unload already in progress\n";
				return false;
			}

			if (!m_inferenceEngines.at(modelName))
			{
				return false;
			}

			m_unloadInProgress = modelName;

			lock.unlock();

			// Start async unloading process
			auto unloadFuture = unloadModelAsync(modelName);

			// Handle unload completion
            m_unloadFutures.emplace_back(std::async(std::launch::async,
                [this, unloadFuture = std::move(unloadFuture), modelName]() mutable {
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
                        m_unloadInProgress = "";

						if (modelName == m_currentModelName)
						{
                            m_modelLoaded = false;
                            resetModelState();
						}
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

            // Prevent concurrent model loading
            if (!m_loadInProgress.empty()) {
                std::cerr << "[ModelManager] Already loading a model, cannot switch now\n";
                return false;
            }

            m_loadInProgress = modelName;

            // Release lock before async operations
            lock.unlock();

            // Start async loading process
            auto loadFuture = loadModelIntoEngineAsync(modelName);

            // Handle load completion
            m_loadFutures.emplace_back(std::async(std::launch::async,
                [this, modelName, loadFuture = std::move(loadFuture), variant]() mutable {
                    bool success = false;
                    try {
                        success = loadFuture.get();
                    }
                    catch (const std::exception& e) {
                        std::cerr << "[ModelManager] Model load error: " << e.what() << "\n";
                    }

                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        m_loadInProgress = "";

                        if (success) {
                            m_modelLoaded = true;
                            std::cout << "[ModelManager] Successfully switched models\n";
                            variant->lastSelected = static_cast<int>(std::time(nullptr));
                            m_persistence->saveModelData(m_models[m_currentModelIndex]);
                        }
                        else {
                            // Clean up the failed engine
                            cleanupFailedEngine(modelName);
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

		bool loadModelIntoEngine(const std::string& modelName)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			// Prevent concurrent model loading
			if (!m_loadInProgress.empty()) {
				std::cerr << "[ModelManager] Already loading a model, cannot load now\n";
				return false;
			}
			m_loadInProgress = modelName;
			// Release lock before async operations
			lock.unlock();
			// Start async loading process
			auto loadFuture = loadModelIntoEngineAsync(modelName);
			// Handle load completion
			m_loadFutures.emplace_back(std::async(std::launch::async,
				[this, modelName, loadFuture = std::move(loadFuture)]() mutable {
					bool success = false;
					try {
						success = loadFuture.get();
					}
					catch (const std::exception& e) {
						std::cerr << "[ModelManager] Model load error: " << e.what() << "\n";
					}
					{
						std::unique_lock<std::shared_mutex> lock(m_mutex);
						m_loadInProgress = "";
						if (success) {
							m_modelLoaded = true;
							std::cout << "[ModelManager] Successfully loaded model\n";
						}
						else {
							// Clean up the failed engine
							cleanupFailedEngine(modelName);
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

        bool isAnyVariantDownloaded(int modelIndex) const {
            const ModelData& model = m_models[modelIndex];
            for (const auto& [variant, _] : model.variants) {
                if (isModelDownloaded(modelIndex, variant)) {
                    return true;
                }
            }
            return false;
        }

        //--------------------------------------------------------------------------------------------
		// Inference Engine
		//--------------------------------------------------------------------------------------------

        ChatCompletionParameters buildChatCompletionParameters(
            const ChatCompletionRequest& request) {
            ChatCompletionParameters params;

            // Copy messages from the request
            for (const auto& msg : request.messages) {
                params.messages.push_back({ msg.role, msg.content });
            }

            // Map parameters from request to our format
            if (request.seed.has_value()) {
                params.randomSeed = request.seed.value();
            }

            if (request.max_tokens.has_value()) {
                params.maxNewTokens = request.max_tokens.value();
            }
            else {
                // Use a reasonable default if not specified
                params.maxNewTokens = 1024;
            }

            params.temperature = request.temperature;
            params.topP = request.top_p;
            params.streaming = request.stream;

			// set seqId to be the current timestamp
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            params.seqId = static_cast<int>(timestamp * 1000 + seqCounter++);

            return params;
        }

        CompletionParameters buildCompletionParameters(const CompletionRequest& request) {
            CompletionParameters params;

            // Set prompt based on request format
            if (std::holds_alternative<std::string>(request.prompt)) {
                params.prompt = std::get<std::string>(request.prompt);
            }
            else if (std::holds_alternative<std::vector<std::string>>(request.prompt)) {
                // Join multiple prompts with newlines if array is provided
                const auto& prompts = std::get<std::vector<std::string>>(request.prompt);
                std::ostringstream joined;
                for (size_t i = 0; i < prompts.size(); ++i) {
                    joined << prompts[i];
                    if (i < prompts.size() - 1) {
                        joined << "\n";
                    }
                }
                params.prompt = joined.str();
            }

            // Map parameters from request to our format
            if (request.seed.has_value()) {
                params.randomSeed = request.seed.value();
            }

            if (request.max_tokens.has_value()) {
                params.maxNewTokens = request.max_tokens.value();
            }
            else {
                // Use a reasonable default if not specified (OpenAI default is 16)
                params.maxNewTokens = 16;
            }

            // Copy other parameters
            params.temperature = request.temperature;
            params.topP = request.top_p;
            params.streaming = request.stream;

            // Set unique sequence ID based on timestamp
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            params.seqId = static_cast<int>(timestamp * 1000 + seqCounter++);

            return params;
        }

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
                completionParams.seqId = currentChat.id;
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
                completionParams.seqId = currentChat.id;
            }

            return completionParams;
        }

        bool stopJob(int jobId, const std::string modelName)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngines.at(modelName))
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return false;
            }

            // Mark the job as inactive in our tracking map
            {
                auto it = m_activeJobs.find(jobId);
                if (it != m_activeJobs.end()) {
                    it->second = false;
                }
            }

            m_inferenceEngines.at(modelName)->stopJob(jobId);
            return true;
        }

        CompletionResult completeSync(const CompletionParameters& params, const std::string modelName)
        {
            CompletionResult emptyResult;
			emptyResult.text = "";
			emptyResult.tps = 0.0F;

            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngines.at(modelName))
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    return emptyResult;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    return emptyResult;
                }
            }

            int jobId = m_inferenceEngines.at(modelName)->submitCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit completions job.\n";
                return emptyResult;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
                m_activeJobs[jobId] = true;
            }

            // Wait for the job to complete
            m_inferenceEngines.at(modelName)->waitForJob(jobId);

            // Get the final result
            CompletionResult result = m_inferenceEngines.at(modelName)->getJobResult(jobId);

            // Check for errors
            if (m_inferenceEngines.at(modelName)->hasJobError(jobId)) {
                std::cerr << "[ModelManager] Error in completion job: "
                    << m_inferenceEngines.at(modelName)->getJobError(jobId) << std::endl;
            }

            // Clean up with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
                m_activeJobs.erase(jobId);
            }

            return result;
        }

        CompletionResult chatCompleteSync(const ChatCompletionParameters& params, const std::string modelName, const bool saveChat = true)
        {
            CompletionResult emptyResult;
			emptyResult.text = "";
			emptyResult.tps = 0.0F;

            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngines.at(modelName))
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    return emptyResult;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    return emptyResult;
                }
            }

            int jobId = m_inferenceEngines.at(modelName)->submitChatCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit chat completions job.\n";
                return emptyResult;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
                m_activeJobs[jobId] = true;
            }

            // Wait for the job to complete
            m_inferenceEngines.at(modelName)->waitForJob(jobId);

            // Get the final result
            CompletionResult result = m_inferenceEngines.at(modelName)->getJobResult(jobId);

            // Check for errors
            if (m_inferenceEngines.at(modelName)->hasJobError(jobId)) {
                std::cerr << "[ModelManager] Error in chat completion job: "
                    << m_inferenceEngines.at(modelName)->getJobError(jobId) << std::endl;
            }

            // Clean up with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
                m_activeJobs.erase(jobId);
            }

            // Save the chat history
            if (saveChat)
            {
                auto& chatManager = Chat::ChatManager::getInstance();
                auto chatName = chatManager.getChatNameByJobId(jobId);
                if (!chatManager.saveChat(chatName))
                {
                    std::cerr << "[ModelManager] Failed to save chat: " << chatName << std::endl;
                }

                // Reset jobid tracking on chat manager
                if (!chatManager.removeJobId(jobId))
                {
                    std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                }
            }

            return result;
        }

        int startCompletionJob(const CompletionParameters& params, std::function<void(const std::string&, 
            const float, const int, const bool)> streamingCallback, const std::string modelName, const bool saveChat = true)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngines.at(modelName))
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

            int jobId = m_inferenceEngines.at(modelName)->submitCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit completions job.\n";
                return -1;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
                m_activeJobs[jobId] = true;
            }

            // Use thread pool instead of creating a detached thread
            m_threadPool.enqueue([this, jobId, streamingCallback, saveChat, modelName]() {
                // Poll while job is running or until the engine says it's done
                while (true)
                {
                    // Check if job was stopped externally
                    {
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        auto it = m_activeJobs.find(jobId);
                        if (it == m_activeJobs.end() || !it->second) break;
                    }

                    if (this->m_inferenceEngines.at(modelName)->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngines.at(modelName)->getJobResult(jobId);
                    bool isFinished = this->m_inferenceEngines.at(modelName)->isJobFinished(jobId);

                    if (!partial.text.empty()) {
                        // Call the user's callback (no need to lock for the callback)
                        if (streamingCallback) {
                            streamingCallback(partial.text, partial.tps, jobId, isFinished);
                        }
                    }

                    if (isFinished) break;

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // Remove job ID with proper synchronization
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
                    m_activeJobs.erase(jobId);
                }

                // Reset jobid tracking on chat manager
                if (saveChat)
                {
                    if (!Chat::ChatManager::getInstance().removeJobId(jobId))
                    {
                        std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                    }
                }
                });

            return jobId;
        }

        int startChatCompletionJob(const ChatCompletionParameters& params, std::function<void(const std::string&, 
            const float, const int, const bool)> streamingCallback, const std::string modelName, const bool saveChat = true)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngines.at(modelName))
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

            int jobId = m_inferenceEngines.at(modelName)->submitChatCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit chat completions job.\n";
                return -1;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
                m_activeJobs[jobId] = true;
            }

            // Use thread pool instead of creating a detached thread
            m_threadPool.enqueue([this, jobId, streamingCallback, saveChat, modelName]() {
                while (true)
                {
                    // Check if job was stopped externally
                    {
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        auto it = m_activeJobs.find(jobId);
                        if (it == m_activeJobs.end() || !it->second) break;
                    }

                    if (this->m_inferenceEngines.at(modelName)->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngines.at(modelName)->getJobResult(jobId);
                    bool isFinished = this->m_inferenceEngines.at(modelName)->isJobFinished(jobId);

                    if (!partial.text.empty()) {
                        // Call the user's callback (no need to lock for the callback)
                        if (streamingCallback) {
                            streamingCallback(partial.text, partial.tps, jobId, isFinished);
                        }
                    }

                    if (isFinished) break;

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // Remove job ID with proper synchronization
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
                    m_activeJobs.erase(jobId);
                }

                if (saveChat)
                {
                    auto& chatManager = Chat::ChatManager::getInstance();

                    // Save the chat history
                    {
                        auto chatName = chatManager.getChatNameByJobId(jobId);
                        if (!chatManager.saveChat(chatName))
                        {
                            std::cerr << "[ModelManager] Failed to save chat: " << chatName << std::endl;
                        }
                    }

                    // Reset jobid tracking on chat manager
                    {
                        if (!chatManager.removeJobId(jobId))
                        {
                            std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                        }
                    }
                }
                });

            return jobId;
        }

        bool isJobFinished(int jobId, const std::string modelName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngines.at(modelName))
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true; // No engine means nothing is running
            }
            return m_inferenceEngines.at(modelName)->isJobFinished(jobId);
        }

        CompletionResult getJobResult(int jobId, const std::string modelName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngines.at(modelName))
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return { {}, "" };
            }
            return m_inferenceEngines.at(modelName)->getJobResult(jobId);
        }

        bool hasJobError(int jobId, const std::string modelName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngines.at(modelName))
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true;
            }
            return m_inferenceEngines.at(modelName)->hasJobError(jobId);
        }

		std::string getJobError(int jobId, const std::string modelName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngines.at(modelName))
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return "Inference engine not initialized";
            }
            return m_inferenceEngines.at(modelName)->getJobError(jobId);
        }

		//--------------------------------------------------------------------------------------------
        // Server management
		//--------------------------------------------------------------------------------------------

        bool startServer(const std::string& port) {
            // Stop any existing server
            kolosal::ServerAPI::instance().shutdown();

            // Initialize logger
            Logger::instance().setLogFile("model_server.log");
            Logger::instance().setLevel(LogLevel::SERVER_INFO);
            Logger::logInfo("Starting model server on port %s", port.c_str());

            // Set chat completion callbacks
            kolosal::ServerAPI::instance().setChatCompletionCallback(
                [this](const ChatCompletionRequest& request) {
                    return this->handleChatCompletionRequest(request);
                }
            );

            kolosal::ServerAPI::instance().setChatCompletionStreamingCallback(
                [this](const ChatCompletionRequest& request,
                    const std::string& requestId,
                    int chunkIndex,
                    ChatCompletionChunk& outputChunk) {
                        return this->handleChatCompletionStreamingRequest(request, requestId, chunkIndex, outputChunk);
                }
            );

            // Set completion callbacks
            kolosal::ServerAPI::instance().setCompletionCallback(
                [this](const CompletionRequest& request) {
                    return this->handleCompletionRequest(request);
                }
            );

            kolosal::ServerAPI::instance().setCompletionStreamingCallback(
                [this](const CompletionRequest& request,
                    const std::string& requestId,
                    int chunkIndex,
                    CompletionChunk& outputChunk) {
                        return this->handleCompletionStreamingRequest(request, requestId, chunkIndex, outputChunk);
                }
            );

            // Initialize and start the server
            if (!kolosal::ServerAPI::instance().init(port)) {
                Logger::logError("Failed to start model server");
                return false;
            }

            Logger::logInfo("Model server started successfully");
            return true;
        }

        void stopServer() {
            Logger::logInfo("Stopping model server");
            kolosal::ServerAPI::instance().shutdown();
        }

        ChatCompletionResponse handleChatCompletionRequest(const ChatCompletionRequest& request) {
            // Build parameters from the incoming request.
            ChatCompletionParameters params = buildChatCompletionParameters(request);
            // (The parameters will include the messages and other fields.)
            params.streaming = false;

            // Invoke the synchronous chat completion method.
            CompletionResult result = chatCompleteSync(params, false);

            // Map the engine’s result to our ChatCompletionResponse.
            ChatCompletionResponse response = convertToChatResponse(request, result);
            return response;
        }

        CompletionResponse handleCompletionRequest(const CompletionRequest& request) {
            // Build parameters from the incoming request
            CompletionParameters params = buildCompletionParameters(request);
            params.streaming = false;

            // Invoke the synchronous completion method
            CompletionResult result = completeSync(params, request.model);

            // Map the engine's result to our CompletionResponse
            CompletionResponse response = convertToCompletionResponse(request, result);
            return response;
        }

        bool handleChatCompletionStreamingRequest(
            const ChatCompletionRequest& request,
            const std::string& requestId,
            int chunkIndex,
            ChatCompletionChunk& outputChunk) {

            // Check if the model name is loaded
            if (!m_inferenceEngines.at(request.model)) {
                Logger::logError("[ModelManager] Inference engine is not initialized.");
                return false;
            }

            // Look up (or create) the ChatCompletionStreamingContext for this requestId.
            std::shared_ptr<ChatCompletionStreamingContext> ctx;
            {
                std::unique_lock<std::mutex> lock(m_streamContextsMutex);
                auto it = m_streamingContexts.find(requestId);
                if (it == m_streamingContexts.end()) {
                    // For the very first chunk (chunkIndex==0) we create a new context.
                    if (chunkIndex == 0) {
                        ctx = std::make_shared<ChatCompletionStreamingContext>();
                        m_streamingContexts[requestId] = ctx;
                    }
                    else {
                        // If no context and chunk index is not zero, something is wrong.
                        Logger::logError("[ModelManager] Streaming context not found for requestId: %s",
                            requestId.c_str());
                        return false;
                    }
                }
                else {
                    ctx = it->second;
                }
            }

            // If this is the first call (chunkIndex 0), start the asynchronous job.
            if (chunkIndex == 0) {
                // Build parameters with streaming enabled.
                ChatCompletionParameters params = buildChatCompletionParameters(request);
                params.streaming = true;

                // Track the job ID and model name for this request
                int jobId = -1;

                {
                    std::lock_guard<std::mutex> lock(ctx->mtx);
                    ctx->model = request.model;
                    ctx->jobId = m_inferenceEngines.at(request.model)->submitChatCompletionsJob(params);
                    jobId = ctx->jobId;
                }

                if (jobId < 0) {
                    Logger::logError("[ModelManager] Failed to submit chat completions job for requestId: %s",
                        requestId.c_str());
                    {
                        std::lock_guard<std::mutex> lock(ctx->mtx);
                        ctx->error = true;
                        ctx->errorMessage = "Failed to start completion job";
                        ctx->finished = true;
                    }
                    {
                        std::unique_lock<std::mutex> lock(m_streamContextsMutex);
                        m_streamingContexts.erase(requestId);
                    }
                    return false;
                }

                // Add job ID with proper synchronization to the global tracking
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_jobIds.push_back(jobId);
                    m_activeJobs[jobId] = true;
                }

                // Use thread pool instead of detached thread
                m_threadPool.enqueue([this, jobId, request, requestId, ctx]() {
                    std::string lastText;
                    auto startTime = std::chrono::steady_clock::now();

                    try {
                        while (true) {
                            // Check if job was stopped externally
                            {
                                std::shared_lock<std::shared_mutex> lock(m_mutex);
                                auto it = m_activeJobs.find(jobId);
                                if (it == m_activeJobs.end() || !it->second) {
                                    std::lock_guard<std::mutex> ctxLock(ctx->mtx);
                                    ctx->finished = true;
                                    break;
                                }
                            }

                            // Check if the job has an error
                            if (this->m_inferenceEngines.at(request.model)->hasJobError(jobId)) {
                                std::string errorMsg = this->m_inferenceEngines.at(request.model)->getJobError(jobId);
                                Logger::logError("[ModelManager] Streaming job error for jobId: %d - %s",
                                    jobId, errorMsg.c_str());
                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->error = true;
                                    ctx->errorMessage = errorMsg;
                                    ctx->finished = true;
                                }
                                ctx->cv.notify_all();
                                break;
                            }

                            // Get the current result and check if finished
                            CompletionResult partial = this->m_inferenceEngines.at(request.model)->getJobResult(jobId);
                            bool isFinished = this->m_inferenceEngines.at(request.model)->isJobFinished(jobId);

                            // Compute delta text (only new text since last poll).
                            std::string newText;
                            if (partial.text.size() > lastText.size()) {
                                newText = partial.text.substr(lastText.size());
                                lastText = partial.text;
                            }

                            // If we have new text, add it to the chunks
                            if (!newText.empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->chunks.push_back(newText);
                                }
                                ctx->cv.notify_all();
                            }

                            // If the job is finished, set the finished flag and break
                            if (isFinished) {
                                auto endTime = std::chrono::steady_clock::now();
                                auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    endTime - startTime).count();

                                Logger::logInfo("[ModelManager] Streaming job %d completed in %lld ms",
                                    jobId, durationMs);

                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->finished = true;
                                }
                                ctx->cv.notify_all();
                                break;
                            }

                            // Sleep briefly to avoid busy-waiting
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }
                    catch (const std::exception& e) {
                        Logger::logError("[ModelManager] Exception in streaming thread: %s", e.what());
                        {
                            std::lock_guard<std::mutex> lock(ctx->mtx);
                            ctx->error = true;
                            ctx->errorMessage = e.what();
                            ctx->finished = true;
                        }
                        ctx->cv.notify_all();
                    }

                    // Clean up job ID tracking
                    {
                        std::unique_lock<std::shared_mutex> lock(this->m_mutex);
                        this->m_jobIds.erase(
                            std::remove(this->m_jobIds.begin(), this->m_jobIds.end(), jobId),
                            this->m_jobIds.end());
                        m_activeJobs.erase(jobId);
                    }
                    });
            }

            if (chunkIndex == 0) {
                // First chunk - just send the role (OpenAI format)
                outputChunk.id = requestId;
                outputChunk.model = request.model;

                ChatCompletionChunkChoice choice;
                choice.index = 0;
                choice.delta.role = "assistant";  // Always "assistant" role for responses
                choice.delta.content = "";        // Empty content in first chunk (just role)
                choice.finish_reason = "";        // No finish reason yet

                outputChunk.choices.clear();
                outputChunk.choices.push_back(choice);

                // More chunks will follow
                return true;
            }
            else {
                // For chunkIndex > 0, wait for the (chunkIndex-1)-th text chunk or completion
                std::unique_lock<std::mutex> lock(ctx->mtx);

                // Wait with a timeout for better responsiveness
                bool result = ctx->cv.wait_for(lock, std::chrono::seconds(30), [ctx, chunkIndex]() {
                    return (ctx->chunks.size() >= static_cast<size_t>(chunkIndex)) ||
                        ctx->finished || ctx->error;
                    });

                if (!result) {
                    // If we timed out
                    Logger::logError("[ModelManager] Timeout waiting for chunk %d for requestId %s",
                        chunkIndex, requestId.c_str());

                    // Clean up and return error
                    std::unique_lock<std::mutex> glock(m_streamContextsMutex);
                    m_streamingContexts.erase(requestId);
                    return false;
                }

                // If an error occurred, clean up the context and signal termination
                if (ctx->error) {
                    Logger::logError("[ModelManager] Error in streaming job for requestId %s: %s",
                        requestId.c_str(), ctx->errorMessage.c_str());

                    std::unique_lock<std::mutex> glock(m_streamContextsMutex);
                    m_streamingContexts.erase(requestId);
                    return false;
                }

                // If job is finished but we don't have this chunk, send a final chunk
                if (ctx->chunks.size() < static_cast<size_t>(chunkIndex) && ctx->finished) {
                    outputChunk.id = requestId;
                    outputChunk.model = ctx->model;

                    ChatCompletionChunkChoice choice;
                    choice.index = 0;
                    choice.delta.content = "";       // Empty content
                    choice.finish_reason = "stop";   // Mark as final chunk

                    outputChunk.choices.clear();
                    outputChunk.choices.push_back(choice);

                    // Clean up the context
                    {
                        std::unique_lock<std::mutex> glock(m_streamContextsMutex);
                        m_streamingContexts.erase(requestId);
                    }

                    return false; // No more chunks to send
                }

                // Get the content for this chunk
                std::string chunkContent = ctx->chunks[chunkIndex - 1];
                outputChunk.id = requestId;
                outputChunk.model = ctx->model;

                ChatCompletionChunkChoice choice;
                choice.index = 0;
                choice.delta.content = chunkContent;
                choice.finish_reason = "";

                outputChunk.choices.clear();
                outputChunk.choices.push_back(choice);

                // Check if this is the last chunk
                bool isLastChunk = ctx->finished && (ctx->chunks.size() == static_cast<size_t>(chunkIndex));

                if (isLastChunk) {
                    // Set finish reason for the last content chunk
                    choice.finish_reason = "stop";
                    outputChunk.choices[0] = choice;

                    // Clean up the context
                    {
                        std::unique_lock<std::mutex> glock(m_streamContextsMutex);
                        m_streamingContexts.erase(requestId);
                    }

                    return false; // No more chunks to send
                }

                // More chunks to come
                return true;
            }
        }

        bool handleCompletionStreamingRequest(
            const CompletionRequest& request,
            const std::string& requestId,
            int chunkIndex,
            CompletionChunk& outputChunk) {

            // Get or create streaming context
            std::shared_ptr<CompletionStreamingContext> ctx;
            {
                std::unique_lock<std::mutex> lock(m_completionStreamContextsMutex);
                auto it = m_completionStreamingContexts.find(requestId);
                if (it == m_completionStreamingContexts.end()) {
                    // For first chunk, create a new context
                    if (chunkIndex == 0) {
                        ctx = std::make_shared<CompletionStreamingContext>();
                        m_completionStreamingContexts[requestId] = ctx;
                    }
                    else {
                        Logger::logError("[ModelManager] Completion streaming context not found for requestId: %s",
                            requestId.c_str());
                        return false;
                    }
                }
                else {
                    ctx = it->second;
                }
            }

            // If this is the first call, start the asynchronous job
            if (chunkIndex == 0) {
                // Build parameters with streaming enabled
                CompletionParameters params = buildCompletionParameters(request);
                params.streaming = true;

                // Track job ID and model for this request
                int jobId = -1;

                {
                    std::lock_guard<std::mutex> lock(ctx->mtx);
                    ctx->model = request.model;

                    // Submit the completion job to the inference engine
                    jobId = m_inferenceEngines.at(request.model)->submitCompletionsJob(params);
                    ctx->jobId = jobId;
                }

                if (jobId < 0) {
                    Logger::logError("[ModelManager] Failed to submit completion job for requestId: %s",
                        requestId.c_str());
                    {
                        std::lock_guard<std::mutex> lock(ctx->mtx);
                        ctx->error = true;
                        ctx->errorMessage = "Failed to start completion job";
                        ctx->finished = true;
                    }
                    {
                        std::unique_lock<std::mutex> lock(m_completionStreamContextsMutex);
                        m_completionStreamingContexts.erase(requestId);
                    }
                    return false;
                }

                // Add job ID to global tracking
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_jobIds.push_back(jobId);
                    m_activeJobs[jobId] = true;
                }

                // Use thread pool instead of detached thread
                m_threadPool.enqueue([this, jobId, request, requestId, ctx]() {
                    std::string lastText;
                    auto startTime = std::chrono::steady_clock::now();

                    try {
                        while (true) {
                            // Check if job was stopped externally
                            {
                                std::shared_lock<std::shared_mutex> lock(m_mutex);
                                auto it = m_activeJobs.find(jobId);
                                if (it == m_activeJobs.end() || !it->second) {
                                    std::lock_guard<std::mutex> ctxLock(ctx->mtx);
                                    ctx->finished = true;
                                    break;
                                }
                            }

                            // Check if the job has an error
                            if (this->m_inferenceEngines.at(request.model)->hasJobError(jobId)) {
                                std::string errorMsg = this->m_inferenceEngines.at(request.model)->getJobError(jobId);
                                Logger::logError("[ModelManager] Streaming completion job error for jobId: %d - %s",
                                    jobId, errorMsg.c_str());
                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->error = true;
                                    ctx->errorMessage = errorMsg;
                                    ctx->finished = true;
                                }
                                ctx->cv.notify_all();
                                break;
                            }

                            // Get the current result and check if finished
                            CompletionResult partial = this->m_inferenceEngines.at(request.model)->getJobResult(jobId);
                            bool isFinished = this->m_inferenceEngines.at(request.model)->isJobFinished(jobId);

                            // Compute delta text (only new text since last poll)
                            std::string newText;
                            if (partial.text.size() > lastText.size()) {
                                newText = partial.text.substr(lastText.size());
                                lastText = partial.text;
                            }

                            // If we have new text, add it to the chunks
                            if (!newText.empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->fullText = lastText;
                                    ctx->chunks.push_back(newText);
                                }
                                ctx->cv.notify_all();
                            }

                            // If the job is finished, set the finished flag and break
                            if (isFinished) {
                                auto endTime = std::chrono::steady_clock::now();
                                auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    endTime - startTime).count();

                                Logger::logInfo("[ModelManager] Streaming completion job %d completed in %lld ms",
                                    jobId, durationMs);

                                {
                                    std::lock_guard<std::mutex> lock(ctx->mtx);
                                    ctx->finished = true;
                                }
                                ctx->cv.notify_all();
                                break;
                            }

                            // Sleep briefly to avoid busy-waiting
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }
                    catch (const std::exception& e) {
                        Logger::logError("[ModelManager] Exception in completion streaming thread: %s", e.what());
                        {
                            std::lock_guard<std::mutex> lock(ctx->mtx);
                            ctx->error = true;
                            ctx->errorMessage = e.what();
                            ctx->finished = true;
                        }
                        ctx->cv.notify_all();
                    }

                    // Clean up job ID tracking
                    {
                        std::unique_lock<std::shared_mutex> lock(this->m_mutex);
                        this->m_jobIds.erase(
                            std::remove(this->m_jobIds.begin(), this->m_jobIds.end(), jobId),
                            this->m_jobIds.end());
                        m_activeJobs.erase(jobId);
                    }
                    });
            }

            // Prepare the chunk response
            outputChunk.id = requestId;
            outputChunk.model = request.model;
            outputChunk.created = static_cast<int64_t>(std::time(nullptr));
            outputChunk.choices.clear();

            // For first chunk, just create an empty choice
            if (chunkIndex == 0) {
                CompletionChunkChoice choice;
                choice.index = 0;
                choice.text = "";
                choice.finish_reason = ""; // Use empty string instead of nullptr
                outputChunk.choices.push_back(choice);
                return true;
            }
            // For subsequent chunks, wait for content
            else {
                std::unique_lock<std::mutex> lock(ctx->mtx);

                // Wait with timeout for the chunk to be available
                bool result = ctx->cv.wait_for(lock, std::chrono::seconds(30), [ctx, chunkIndex]() {
                    return (ctx->chunks.size() >= static_cast<size_t>(chunkIndex)) ||
                        ctx->finished || ctx->error;
                    });

                if (!result) {
                    // Timeout occurred
                    Logger::logError("[ModelManager] Timeout waiting for completion chunk %d for requestId %s",
                        chunkIndex, requestId.c_str());

                    // Keep the lock when we check if this is the last message
                    std::unique_lock<std::mutex> glock(m_completionStreamContextsMutex);
                    m_completionStreamingContexts.erase(requestId);
                    return false;
                }

                // Handle errors - still holding the lock
                if (ctx->error) {
                    Logger::logError("[ModelManager] Error in streaming completion for requestId %s: %s",
                        requestId.c_str(), ctx->errorMessage.c_str());

                    // Keep the lock when we check if this is the last message
                    std::unique_lock<std::mutex> glock(m_completionStreamContextsMutex);
                    m_completionStreamingContexts.erase(requestId);
                    return false;
                }

                CompletionChunkChoice choice;
                choice.index = 0;

                // Check for completion state while still holding the lock
                bool hasChunk = ctx->chunks.size() >= static_cast<size_t>(chunkIndex);
                bool isFinished = ctx->finished;
                bool isLastChunk = false;

                if (hasChunk) {
                    // Get the content for this chunk while holding the lock
                    choice.text = ctx->chunks[chunkIndex - 1];

                    // Determine if this is the last chunk while safely protected by the lock
                    isLastChunk = isFinished && (ctx->chunks.size() == static_cast<size_t>(chunkIndex));
                    choice.finish_reason = isLastChunk ? "stop" : ""; // Use empty string instead of nullptr
                }
                else if (isFinished) {
                    // No chunk but job is finished - send empty final chunk
                    choice.text = "";
                    choice.finish_reason = "stop";
                    isLastChunk = true;
                }
                else {
                    // We have no chunk yet but still waiting
                    choice.text = "";
                    choice.finish_reason = ""; // Use empty string instead of nullptr
                }

                // Release the ctx lock before acquiring the global contexts lock to avoid deadlock
                lock.unlock();

                // Clean up if this is the last chunk
                if (isLastChunk) {
                    std::unique_lock<std::mutex> glock(m_completionStreamContextsMutex);
                    m_completionStreamingContexts.erase(requestId);
                }

                outputChunk.choices.push_back(choice);
                return !isLastChunk; // Return true if more chunks remain, false if this is the last one
            }
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
                unloadModel(m_models[modelIndex].name);
            }

            // Call the persistence layer to delete the file - passing the variant type instead of the variant
            m_persistence->deleteModelVariant(m_models[modelIndex], variantType);
            return true;
        }

        void resetModelState() {
            m_currentModelName = std::nullopt;
            m_currentVariantType = "";
            m_currentModelIndex = 0;
            m_modelLoaded = false;
        }

        void cleanupFailedEngine(const std::string& modelName) {
            auto it = m_inferenceEngines.find(modelName);
            if (it != m_inferenceEngines.end()) {
                // Release resources if the engine implementation requires it
                if (it->second) {
                    it->second->unloadModel();
                }
                m_inferenceEngines.erase(it);
            }
        }

        bool retryModelLoad(const std::string& modelName, const std::string& variantType) {
            // First clean up any previous failed attempt
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                cleanupFailedEngine(modelName);
            }

            // Then try to switch to this model again
            return switchModel(modelName, variantType);
        }

		bool isCurrentlyGenerating() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_modelGenerationInProgress;
		}

		bool setModelGenerationInProgress(bool inProgress)
		{
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
			return !m_loadInProgress.empty();
		}

		std::string getCurrentOnLoadingModel() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_loadInProgress;
		}

		bool isUnloadInProgress() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return !m_unloadInProgress.empty();
		}

		std::string getCurrentOnUnloadingModel() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_unloadInProgress;
		}

        bool isModelLoaded(const std::string& modelName) const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_inferenceEngines.find(modelName);
            if (it != m_inferenceEngines.end())
            {
                return it->second != nullptr;
            }
            return false;
        }

        bool hasEnoughMemoryForModel(const std::string& modelName, float& memoryReqBuff, float& kvReqBuff) {
            auto it = m_modelNameToIndex.find(modelName);
            if (it == m_modelNameToIndex.end()) {
                std::cerr << "[ModelManager] Model not found: " << modelName << "\n";
                return false;
            }

            size_t modelIndex = it->second;
            const auto& model = m_models[modelIndex];
            const auto& variant = model.variants.at(
                getCurrentVariantForModel(modelName)
            );

            // Calculate model size in bytes (convert from GB)
            size_t modelSizeBytes = static_cast<size_t>(variant.size * 1024 * 1024 * 1024);

            // Calculate KV cache size based on model parameters
            // KV cache formula: 2 (key & value) * hidden_size * hidden_layers * max_seq_length * bytes_per_token
            const size_t MAX_SEQUENCE_LENGTH = ModelLoaderConfigManager::getInstance().getConfig().n_ctx;
            
            float_t kvCacheSizeBytes = 4 *
                model.hidden_size *
                model.hidden_layers *
                MAX_SEQUENCE_LENGTH;

            // Update the buffers in MB
			memoryReqBuff = (modelSizeBytes) / (1024 * 1024);
			kvReqBuff = (kvCacheSizeBytes) / (1024 * 1024);

            // Check if we have enough memory using SystemMonitor
            auto& sysMonitor = SystemMonitor::getInstance();
            bool hasEnoughMemory = sysMonitor.hasEnoughMemoryForModel(
                modelSizeBytes,
                kvCacheSizeBytes
            );

            return hasEnoughMemory;
        }

        bool hasEnoughMemoryForModel(const std::string& modelName) {
            auto it = m_modelNameToIndex.find(modelName);
            if (it == m_modelNameToIndex.end()) {
                std::cerr << "[ModelManager] Model not found: " << modelName << "\n";
                return false;
            }

            size_t modelIndex = it->second;
            const auto& model = m_models[modelIndex];
            const auto& variant = model.variants.at(
                getCurrentVariantForModel(modelName)
            );

            // Calculate model size in bytes (convert from GB)
            size_t modelSizeBytes = static_cast<size_t>(variant.size * 1024 * 1024 * 1024);

            // Calculate KV cache size based on model parameters
            // KV cache formula: 2 (key & value) * hidden_size * hidden_layers * max_seq_length * bytes_per_token
            const size_t MAX_SEQUENCE_LENGTH = ModelLoaderConfigManager::getInstance().getConfig().n_ctx;
            const size_t BYTES_PER_TOKEN = 2; // Assuming FP16 precision kv (2 bytes)

            size_t kvCacheSizeBytes = 4 *
                model.hidden_size *
                model.hidden_layers *
                MAX_SEQUENCE_LENGTH;

            // Check if we have enough memory using SystemMonitor
            auto& sysMonitor = SystemMonitor::getInstance();
            bool hasEnoughMemory = sysMonitor.hasEnoughMemoryForModel(
                modelSizeBytes,
                kvCacheSizeBytes
            );

            return hasEnoughMemory;
        }

        bool addCustomModel(const Model::ModelData modelData)
        {
			std::unique_lock<std::shared_mutex> lock(m_mutex);

            if (m_modelNameToIndex.count(modelData.name)) {
                std::cerr << "[ModelManager] Model with name '" << modelData.name << "' already exists.\n";
                return false;
            }

            if (modelData.variants.empty()) {
                std::cerr << "[ModelManager] Cannot add model with no variants\n";
                return false;
            }

			m_models.push_back(modelData);
			m_modelNameToIndex[modelData.name] = m_models.size() - 1;

			// save the model to persistence
			m_persistence->saveModelData(modelData);

			// Update the model variant map
			m_modelVariantMap[modelData.name] = modelData.variants.begin()->first;
			return true;
		}

		const bool isUsingGpu() const {
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_isVulkanBackend;
		}

    private:
        explicit ModelManager(std::unique_ptr<IModelPersistence> persistence, const bool async = true)
            : m_persistence(std::move(persistence))
            , m_currentModelName(std::nullopt)
            , m_currentModelIndex(0)
            , m_inferenceLibHandle(nullptr)
            , m_createInferenceEnginePtr(nullptr)
			, m_modelLoaded(false)
            , m_modelGenerationInProgress(false)
        {
            if (async)
            {
                startAsyncInitialization();
                return;
            }

			// Load inference engine backend and models synchronously
			loadModels();
			m_isVulkanBackend = useVulkanBackend();
			std::string backendName = "InferenceEngineLib.dll";
            if (m_isVulkanBackend)
            {
                backendName = "InferenceEngineLibVulkan.dll";
            }

			if (!loadInferenceEngineDynamically(backendName.c_str()))
			{
				std::cerr << "[ModelManager] Failed to load inference engine for backend: "
					<< backendName << std::endl;
				return;
			}
        }

        ~ModelManager()
        {
            stopAllJobs();
            cancelAllDownloads();

            if (m_initializationFuture.valid()) {
                m_initializationFuture.wait();
            }

			// Clean up all inference engines
            if (!m_inferenceEngines.empty())
            {
                for (auto& [modelName, engine] : m_inferenceEngines)
                {
                    if (engine && m_destroyInferenceEnginePtr)
                    {
                        m_destroyInferenceEnginePtr(engine);
                    }

                    engine = nullptr;
                    m_inferenceEngines.erase(modelName);
                }
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
            m_initializationFuture = m_threadPool.enqueue([this]() {
                auto& sysMonitor = SystemMonitor::getInstance();
                sysMonitor.update();

                loadModels();  // blocking
                m_isVulkanBackend = useVulkanBackend();
                std::string backendName = "InferenceEngineLib.dll";

                if (m_isVulkanBackend)
                {
					backendName = "InferenceEngineLibVulkan.dll";
                    SystemMonitor::getInstance().initializeGpuMonitoring();
                }

                if (!loadInferenceEngineDynamically(backendName)) {
                    std::cerr << "Failed to load inference engine\n";
                    return;
                }

                std::optional<std::string> name;
                {
                    std::unique_lock lock(m_mutex);
                    if (m_currentModelName.has_value()) {
                        m_loadInProgress = m_currentModelName.value();
                        name = m_currentModelName;
                    }
                }

                if (name.has_value()) {
                    auto future = loadModelIntoEngineAsync(name.value());
					if (!future.get()) {
						std::cerr << "Failed to load model into engine\n";
						resetModelState();
					}
                }

                std::unique_lock lock(m_mutex);
                m_loadInProgress.clear();
                });
        }

        void loadModels()
        {
            // Load all models from persistence.
            auto loadedModels = m_persistence->loadAllModels().get();

            // Merge any duplicate models by name.
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
                    // Merge variants based on last selected time
                    for (auto& [type, variant] : model.variants)
                    {
                        auto existingIt = it->second.variants.find(type);
                        if (existingIt == it->second.variants.end() ||
                            variant.lastSelected > existingIt->second.lastSelected)
                        {
                            it->second.variants[type] = variant;
                        }
                    }
                }
            }

            // Rebuild the models vector.
            std::vector<ModelData> models;
            models.reserve(mergedModels.size());
            for (auto& pair : mergedModels)
            {
                models.push_back(pair.second);
            }

            // Check and fix each variant's download status.
            for (auto& model : models)
            {
                for (auto& [type, variant] : model.variants)
                {
                    checkAndFixDownloadStatus(variant);
                }
            }

            // Update internal state under lock.
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_models = std::move(models);
                m_modelNameToIndex.clear();
                m_modelVariantMap.clear();

                // For each model, choose the "best" variant based on lastSelected and downloaded state
                for (size_t i = 0; i < m_models.size(); ++i)
                {
                    m_modelNameToIndex[m_models[i].name] = i;
                    int bestEffectiveValue = -1;
                    std::string bestVariant;

                    // Check each variant
                    for (const auto& [type, variant] : m_models[i].variants)
                    {
                        int effectiveValue = variant.lastSelected;
                        if (variant.isDownloaded)
                        {
                            effectiveValue += 1000000;  // boost for downloaded variants
                        }
                        if (effectiveValue > bestEffectiveValue)
                        {
                            bestEffectiveValue = effectiveValue;
                            bestVariant = type;
                        }
                    }

                    // If no variant was ever selected, default to first variant or empty
                    if (bestVariant.empty() && !m_models[i].variants.empty())
                    {
                        bestVariant = m_models[i].variants.begin()->first;
                    }

                    if (!bestVariant.empty())
                    {
                        m_modelVariantMap[m_models[i].name] = bestVariant;
                    }
                }
            }

            // Determine the overall current model selection (for loading into the engine).
            int maxLastSelected = -1;
            size_t selectedModelIndex = 0;
            std::string selectedVariantType;
            for (size_t i = 0; i < m_models.size(); ++i)
            {
                const auto& model = m_models[i];

                for (const auto& [type, variant] : model.variants)
                {
                    if (variant.isDownloaded && variant.lastSelected > maxLastSelected)
                    {
                        maxLastSelected = variant.lastSelected;
                        selectedModelIndex = i;
                        selectedVariantType = type;
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

        ModelVariant* getVariantLocked(size_t modelIndex, const std::string& variantType)
        {
            if (modelIndex >= m_models.size())
                return nullptr;

            auto& model = m_models[modelIndex];
            auto it = model.variants.find(variantType);
            if (it != model.variants.end()) {
                return &it->second;
            }
            return nullptr;
        }

        const ModelVariant* getVariantLocked(size_t modelIndex, const std::string& variantType) const
        {
            if (modelIndex >= m_models.size())
                return nullptr;

            const auto& model = m_models[modelIndex];
            auto it = model.variants.find(variantType);
            if (it != model.variants.end()) {
                return &it->second;
            }
            return nullptr;
        }

        void startDownloadAsyncLocked(size_t modelIndex, const std::string& variantType)
        {
            if (modelIndex >= m_models.size())
                return;

            ModelVariant* variant = getVariantLocked(modelIndex, variantType);
            if (!variant)
                return;

			const std::string modelName = m_models[modelIndex].name;

            variant->downloadProgress = 0.01f;  // 0% looks like no progress

            // Begin the asynchronous download - passing the variant type rather than the variant itself
            auto downloadFuture = m_persistence->downloadModelVariant(m_models[modelIndex], variantType);

            // Chain a continuation that waits for the download to complete.
            m_downloadFutures.emplace_back(std::async(std::launch::async,
                [this, modelIndex, modelName, variantType, fut = std::move(downloadFuture)]() mutable {
                    // Wait for the download to finish.
                    fut.wait();

                    // After download, check if this model variant is still the current selection.
                    {
                        std::unique_lock<std::shared_mutex> lock(m_mutex);
                        if (m_currentModelIndex == modelIndex && m_currentVariantType == variantType)
                        {
                            // Unlock before loading the model.
                            lock.unlock();

                            auto loadFuture = loadModelIntoEngineAsync(modelName);
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

#ifdef DEBUG
			std::cout << "[ModelManager] Successfully loaded inference engine from: "
				<< backendName << std::endl;
#endif

#endif
            return true;
        }

        std::future<bool> loadModelIntoEngineAsync(const std::string& modelName) {
            if (!hasEnoughMemoryForModel(modelName)) {
                std::promise<bool> promise;
                promise.set_value(false);
                return promise.get_future();
            }

            std::optional<std::string> modelDir;
            {
                std::shared_lock lock(m_mutex);
                int index = m_modelNameToIndex[modelName];
                auto variant = getVariantLocked(index, getCurrentVariantForModel(modelName));
                if (!variant || !variant->isDownloaded) {
                    std::promise<bool> promise;
                    promise.set_value(false);
                    return promise.get_future();
                }

                modelDir = std::filesystem::absolute(
                    variant->path.substr(0, variant->path.find_last_of("/\\"))).string();
            }

            return m_threadPool.enqueue([this, modelName, modelDir]() {
				std::cout << "[ModelManager] size of inference engines: " << sizeof(m_inferenceEngines) << std::endl;

                auto engine = m_createInferenceEnginePtr();
                if (!engine) return false;

                bool success = engine->loadModel(modelDir->c_str(), ModelLoaderConfigManager::getInstance().getConfig());
                if (success) {
                    std::unique_lock lock(m_mutex);
                    m_inferenceEngines[modelName] = engine;
                    std::cout << "[ModelManager] size of inference engines: " << sizeof(m_inferenceEngines) << std::endl;
                    m_modelLoaded = true;
                }
                else {
                    std::cerr << "Model load failed\n";
                }

                return success;
                });
        }

        std::future<bool> ModelManager::unloadModelAsync(const std::string modelName) {
            // Capture current loaded state under lock
            bool isLoaded;
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
				// Check if the model is loaded in m_inferenceEngines
				isLoaded = m_inferenceEngines.find(modelName) != m_inferenceEngines.end();

                if (!isLoaded) {
                    std::cerr << "[ModelManager] No model loaded to unload\n";
                    return std::async(std::launch::deferred, [] { return false; });
                }
            }

            // Launch heavy unloading in async task
            return std::async(std::launch::async, [this, modelName]() {
                try {
                    bool success = m_inferenceEngines.at(modelName)->unloadModel();
					// delete the engine instance
					m_destroyInferenceEnginePtr(m_inferenceEngines.at(modelName));
					m_inferenceEngines.erase(modelName);

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
            std::vector<int> jobs;
            {
                std::shared_lock lock(m_mutex);
                jobs = m_jobIds;

                for (int id : jobs) {
                    m_activeJobs[id] = false; // Mark jobs as inactive
                }
            }

            for (int id : jobs) {
                for (auto& [name, engine] : m_inferenceEngines)
                    engine->stopJob(id);
            }
        }

        void cancelAllDownloads() {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (auto& model : m_models) {
                for (auto& [type, variant] : model.variants) {
                    // If download is in progress (between 0 and 100), set cancel flag
                    if (variant.downloadProgress > 0.0 && variant.downloadProgress < 100.0) {
                        variant.cancelDownload = true;
                    }
                }
            }
        }

        static ChatCompletionResponse convertToChatResponse(
            const ChatCompletionRequest& request, const CompletionResult& result)
        {
            ChatCompletionResponse response;
            response.model = request.model;

            ChatCompletionChoice choice;
            choice.index = 0;
            choice.message.role = "assistant";
            choice.message.content = result.text;
            // For simplicity we assume the response is complete.
            choice.finish_reason = "stop";

            response.choices.push_back(choice);
            // For usage we make a simple estimate (adjust as needed)
            response.usage.prompt_tokens = 0;
            response.usage.completion_tokens =
                static_cast<int>(result.text.size() / 5);
            response.usage.total_tokens =
                response.usage.prompt_tokens + response.usage.completion_tokens;

            return response;
        }

        static CompletionResponse convertToCompletionResponse(const CompletionRequest& request, const CompletionResult& result) {
            CompletionResponse response;
            response.model = request.model;

            // Create a choice with the generated text
            CompletionChoice choice;
            choice.index = 0;
            choice.text = result.text;
            choice.finish_reason = "stop"; // Assuming completion finished normally

            response.choices.push_back(choice);

            // Set usage statistics - this is an estimation
            int promptLength = 0;
            if (std::holds_alternative<std::string>(request.prompt)) {
                promptLength = std::get<std::string>(request.prompt).size() / 4; // Rough token estimation
            }
            else if (std::holds_alternative<std::vector<std::string>>(request.prompt)) {
                for (const auto& p : std::get<std::vector<std::string>>(request.prompt)) {
                    promptLength += p.size() / 4;
                }
            }

            int completionLength = result.text.size() / 4; // Rough token estimation

            response.usage.prompt_tokens = promptLength;
            response.usage.completion_tokens = completionLength;
            response.usage.total_tokens = promptLength + completionLength;

            return response;
        }

        ThreadPool m_threadPool{ std::max(4u, std::thread::hardware_concurrency() - 1) };
        std::unordered_map<int, std::atomic<bool>> m_activeJobs;

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
		std::future<void>                               m_persistenceFuture;
        std::vector<std::future<void>>                  m_loadFutures;
        std::vector<std::future<void>>                  m_unloadFutures;
		std::string                                     m_unloadInProgress;
        std::string                                     m_loadInProgress;
        std::unordered_map<std::string, std::string>    m_modelVariantMap;
        std::atomic<bool>                               m_modelLoaded{ false };
		std::atomic<bool>                               m_modelGenerationInProgress{ false };
        std::vector<int>                                m_jobIds;
		bool                                            m_isVulkanBackend{ false };

#ifdef _WIN32
        HMODULE m_inferenceLibHandle = nullptr;
#endif

        CreateInferenceEngineFunc*  m_createInferenceEnginePtr  = nullptr;
        DestroyInferenceEngineFunc* m_destroyInferenceEnginePtr = nullptr;

		std::map<const std::string, IInferenceEngine*> m_inferenceEngines;

		// Server related
        struct ChatCompletionStreamingContext {
            std::mutex mtx;
            std::condition_variable cv;
            std::vector<std::string> chunks;
            std::string model;        // Store model name
            int jobId = -1;           // Store job ID
            std::string errorMessage; // Store error details
            bool finished = false;
            bool error = false;
        };
        std::mutex m_streamContextsMutex;
        std::unordered_map<std::string, std::shared_ptr<ChatCompletionStreamingContext>>
            m_streamingContexts;

        struct CompletionStreamingContext {
            std::mutex mtx;
            std::condition_variable cv;
            std::string model;
            int jobId = -1;
            std::vector<std::string> chunks;
            bool finished = false;
            bool error = false;
            std::string errorMessage;
            std::string fullText; // Accumulated full text
        };
        std::mutex m_completionStreamContextsMutex;
        std::unordered_map<std::string, std::shared_ptr<CompletionStreamingContext>> 
            m_completionStreamingContexts;
    };

    inline void initializeModelManager(const bool async = true)
    {
        ModelManager::getInstance(async);
    }

    inline void initializeModelManagerWithCustomPersistence(std::unique_ptr<IModelPersistence> persistence)
    {
        ModelManager::getInstance().initialize(std::move(persistence));
    }

} // namespace Model