#pragma once

#include "preset_manager.hpp"
#include "model_persistence.hpp"
#include "model_loader_config_manager.hpp"

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

        CompletionResult completeSync(const CompletionParameters& params)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngine)
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    CompletionResult result;
                    result.text = "";
                    result.tps = 0.0F;
                    return result;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    CompletionResult result;
                    result.text = "";
                    result.tps = 0.0F;
                    return result;
                }
            }

            int jobId = m_inferenceEngine->submitCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit completions job.\n";
                CompletionResult result;
                result.text = "";
                result.tps = 0.0F;
                return result;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
            }

            // Wait for the job to complete
            m_inferenceEngine->waitForJob(jobId);

            // Get the final result
            CompletionResult result = m_inferenceEngine->getJobResult(jobId);

            // Check for errors
            if (m_inferenceEngine->hasJobError(jobId)) {
                std::cerr << "[ModelManager] Error in completion job: "
                    << m_inferenceEngine->getJobError(jobId) << std::endl;
            }

            // Clean up with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
            }

            return result;
        }

        CompletionResult chatCompleteSync(const ChatCompletionParameters& params, const bool saveChat = true)
        {
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                if (!m_inferenceEngine)
                {
                    std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                    CompletionResult result;
                    result.text = "";
                    result.tps = 0.0F;
                    return result;
                }
                if (!m_modelLoaded)
                {
                    std::cerr << "[ModelManager] No model is currently loaded.\n";
                    CompletionResult result;
                    result.text = "";
                    result.tps = 0.0F;
                    return result;
                }
            }

            int jobId = m_inferenceEngine->submitChatCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit chat completions job.\n";
                CompletionResult result;
                result.text = "";
                result.tps = 0.0F;
                return result;
            }

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
            }

            // Wait for the job to complete
            m_inferenceEngine->waitForJob(jobId);

            // Get the final result
            CompletionResult result = m_inferenceEngine->getJobResult(jobId);

            // Check for errors
            if (m_inferenceEngine->hasJobError(jobId)) {
                std::cerr << "[ModelManager] Error in chat completion job: "
                    << m_inferenceEngine->getJobError(jobId) << std::endl;
            }

            // Clean up with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.erase(std::remove(m_jobIds.begin(), m_jobIds.end(), jobId), m_jobIds.end());
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

        int startCompletionJob(const CompletionParameters& params, std::function<void(const std::string&, const float, const int, const bool)> streamingCallback, const bool saveChat = true)
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

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
            }

            std::thread([this, jobId, streamingCallback, saveChat]() {
                // Poll while job is running or until the engine says it's done
                while (true)
                {
                    if (this->m_inferenceEngine->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngine->getJobResult(jobId);
                    bool isFinished = this->m_inferenceEngine->isJobFinished(jobId);

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
                }

                // Reset jobid tracking on chat manager
                {
                    if (saveChat)
                    {
                        if (!Chat::ChatManager::getInstance().removeJobId(jobId))
                        {
                            std::cerr << "[ModelManager] Failed to remove job id from chat manager.\n";
                        }
                    }
                }
                }).detach();

            return jobId;
        }

        int startChatCompletionJob(const ChatCompletionParameters& params, std::function<void(const std::string&, const float, const int, const bool)> streamingCallback, const bool saveChat = true)
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

            // Add job ID with proper synchronization
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_jobIds.push_back(jobId);
            }

            std::thread([this, jobId, streamingCallback, saveChat]() {
                while (true)
                {
                    if (this->m_inferenceEngine->hasJobError(jobId)) break;

                    CompletionResult partial = this->m_inferenceEngine->getJobResult(jobId);
                    bool isFinished = this->m_inferenceEngine->isJobFinished(jobId);

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
                }).detach();

            return jobId;
        }

        bool isJobFinished(int jobId)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true; // No engine means nothing is running
            }
            return m_inferenceEngine->isJobFinished(jobId);
        }

        CompletionResult getJobResult(int jobId)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return { {}, "" };
            }
            return m_inferenceEngine->getJobResult(jobId);
        }

        bool hasJobError(int jobId)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return true;
            }
            return m_inferenceEngine->hasJobError(jobId);
        }

        std::string getJobError(int jobId)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_inferenceEngine)
            {
                std::cerr << "[ModelManager] Inference engine is not initialized.\n";
                return "Inference engine not initialized";
            }
            return m_inferenceEngine->getJobError(jobId);
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

            // Set inference callbacks
            kolosal::ServerAPI::instance().setInferenceCallback(
                [this](const ChatCompletionRequest& request) {
                    return this->handleNonStreamingRequest(request);
                }
            );

            kolosal::ServerAPI::instance().setStreamingInferenceCallback(
                [this](const ChatCompletionRequest& request,
                    const std::string& requestId,
                    int chunkIndex,
                    ChatCompletionChunk& outputChunk) {
                        return this->handleStreamingRequest(request, requestId, chunkIndex, outputChunk);
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

        ChatCompletionResponse handleNonStreamingRequest(const ChatCompletionRequest& request) {
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

        bool ModelManager::handleStreamingRequest(
            const ChatCompletionRequest& request,
            const std::string& requestId,
            int chunkIndex,
            ChatCompletionChunk& outputChunk) {
            // Look up (or create) the StreamingContext for this requestId.
            std::shared_ptr<StreamingContext> ctx;
            {
                std::unique_lock<std::mutex> lock(m_streamContextsMutex);
                auto it = m_streamingContexts.find(requestId);
                if (it == m_streamingContexts.end()) {
                    // For the very first chunk (chunkIndex==0) we create a new context.
                    if (chunkIndex == 0) {
                        ctx = std::make_shared<StreamingContext>();
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
                    ctx->jobId = m_inferenceEngine->submitChatCompletionsJob(params);
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
                }

                // Launch an asynchronous thread that polls the job and accumulates new text.
                std::thread([this, jobId, requestId, ctx]() {
                    std::string lastText;
                    auto startTime = std::chrono::steady_clock::now();

                    try {
                        while (true) {
                            // Check if the job has an error
                            if (this->m_inferenceEngine->hasJobError(jobId)) {
                                std::string errorMsg = this->m_inferenceEngine->getJobError(jobId);
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
                            CompletionResult partial = this->m_inferenceEngine->getJobResult(jobId);
                            bool isFinished = this->m_inferenceEngine->isJobFinished(jobId);

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
                    }

                    // We don't erase the streaming context here - that happens when the last chunk is requested
                    }).detach();
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
                    bool success = m_inferenceEngine->loadModel(modelDir->c_str(),
                        ModelLoaderConfigManager::getInstance().getConfig());

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
            std::vector<int> jobIdsCopy;
            {
                std::shared_lock<std::shared_mutex> lock(m_mutex);
                jobIdsCopy = m_jobIds;
            }

            for (auto jobId : jobIdsCopy)
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

		// Server related
        struct StreamingContext {
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
        std::unordered_map<std::string, std::shared_ptr<StreamingContext>>
            m_streamingContexts;
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