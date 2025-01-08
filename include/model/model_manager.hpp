#pragma once

#include "model_persistence.hpp"

#include <inference.h>
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

typedef InferenceEngine& (GetInferenceEngineFunc)();

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
            loadModelsAsync();
        }

        // Switch to a specific model variant. If not downloaded, trigger download.
        bool switchModel(const std::string &modelName, const std::string &variantType)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_modelNameToIndex.find(modelName);
            if (it == m_modelNameToIndex.end())
            {
                return false; // Model not found
            }

            m_currentModelName = modelName;
            m_currentVariantType = variantType;
            m_currentModelIndex = it->second;

            // If not downloaded, start download
            ModelVariant *variant = getVariantLocked(m_currentModelIndex, m_currentVariantType);
            if (variant)
            {
                if (!variant->isDownloaded && variant->downloadProgress == 0.0)
                {
                    startDownloadAsyncLocked(m_currentModelIndex, m_currentVariantType);
                }
                else
                {
                    // Update lastSelected
                    variant->lastSelected = static_cast<int>(std::time(nullptr));
                    m_persistence->saveModelData(m_models[m_currentModelIndex]);

					// Load model into inference engine
                    {
                        // Release the lock to avoid potential deadlock
                        lock.unlock();

                        if (!loadModelIntoEngine())
                        {
                            std::cerr << "[ModelManager] Failed to load model into inference engine.\n";
                            return false;
                        }
                    }
                }
            }

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

        void setStreamingCallback(std::function<void(const std::string&, const int)> callback)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_streamingCallback = std::move(callback);
        }

        int startCompletionJob(const CompletionParameters& params)
        {
			InferenceEngine& engine = m_getInferenceEnginePtr();

            int jobId = engine.submitCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit completions job.\n";
                return -1;
            }

            std::thread([this, jobId]() {
                InferenceEngine& engine = m_getInferenceEnginePtr();

                // Poll while job is running or until the engine says it's done
                while (true)
                {
                    if (engine.isJobFinished(jobId)) break;
                    if (engine.hasJobError(jobId)) break;

                    CompletionResult partial = engine.getJobResult(jobId);

                    if (!partial.text.empty()) {
                        // Call the user’s callback
                        // (hold shared lock if needed to be thread-safe)
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        if (m_streamingCallback) {
                            m_streamingCallback(partial.text, jobId);
                        }
                    }

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                }).detach();

            return jobId;
        }

        int startChatCompletionJob(const ChatCompletionParameters& params)
        {
            InferenceEngine& engine = m_getInferenceEnginePtr();

            int jobId = engine.submitChatCompletionsJob(params);
            if (jobId < 0) {
                std::cerr << "[ModelManager] Failed to submit chat completions job.\n";
                return -1;
            }

            std::thread([this, jobId]() {
                InferenceEngine& engine = m_getInferenceEnginePtr();

                // Poll while job is running or until the engine says it's done
                while (true)
                {
                    if (engine.hasJobError(jobId)) break;

                    CompletionResult partial = engine.getJobResult(jobId);

                    if (!partial.text.empty()) {
                        // Call the user’s callback
                        std::shared_lock<std::shared_mutex> lock(m_mutex);
                        if (m_streamingCallback) {
                            m_streamingCallback(partial.text, jobId);
                        }
                    }

                    if (engine.isJobFinished(jobId)) break;

                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                }).detach();

            return jobId;
        }

        bool isJobFinished(int jobId)
        {
			InferenceEngine& engine = m_getInferenceEnginePtr();
            return engine.isJobFinished(jobId);
        }

        CompletionResult getJobResult(int jobId)
        {
			InferenceEngine& engine = m_getInferenceEnginePtr();
            return engine.getJobResult(jobId);
        }

        bool hasJobError(int jobId)
        {
			InferenceEngine& engine = m_getInferenceEnginePtr();
            return engine.hasJobError(jobId);
        }

        std::string getJobError(int jobId)
        {
            InferenceEngine& engine = m_getInferenceEnginePtr();
            return engine.getJobError(jobId);
        }

    private:
        explicit ModelManager(std::unique_ptr<IModelPersistence> persistence)
            : m_persistence(std::move(persistence))
            , m_currentModelName(std::nullopt)
            , m_currentModelIndex(0)
            , m_inferenceLibHandle(nullptr)
            , m_getInferenceEnginePtr(nullptr)
        {
            loadModelsAsync();

			// Select which inference engine library to load
            std::string backendName = useVulkanBackend() ? 
                "InferenceEngineLibVulkan.dll" : "InferenceEngineLib.dll";

            if (!loadInferenceEngineDynamically(backendName)) {
                std::cerr << "[ModelManager] Failed to load inference engine for backend: "
                    << backendName << std::endl;
                return;
            }

            if (!loadModelIntoEngine())
            {
                std::cerr << "[ModelManager] Failed to load model into inference engine.\n";
            }
        }

        ~ModelManager()
        {
            if (m_inferenceLibHandle) {
#ifdef _WIN32
                FreeLibrary(m_inferenceLibHandle);
#endif
                m_inferenceLibHandle = nullptr;
                m_getInferenceEnginePtr = nullptr;
            }
        }

        void loadModelsAsync() 
        {
            std::async(std::launch::async, [this]() {
                auto models = m_persistence->loadAllModels().get();

                // After loading, check file existence if isDownloaded is true
                for (auto& model : models) 
                {
                    checkAndFixDownloadStatus(model.fullPrecision);
                    checkAndFixDownloadStatus(model.quantized4Bit);
                }

                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_models = std::move(models);
                m_modelNameToIndex.clear();

                for (size_t i = 0; i < m_models.size(); ++i) 
                {
                    m_modelNameToIndex[m_models[i].name] = i;
                }

                // Find the model variant with the highest lastSelected value
                int maxLastSelected = -1;
                size_t selectedModelIndex = 0;
                std::string selectedVariantType;

                for (size_t i = 0; i < m_models.size(); ++i)
                {
                    const auto& model = m_models[i];
                    const ModelVariant* variants[] = { &model.quantized4Bit, &model.fullPrecision };

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

                if (maxLastSelected >= 0)
                {
                    m_currentModelName = m_models[selectedModelIndex].name;
                    m_currentModelIndex = selectedModelIndex;
                    m_currentVariantType = selectedVariantType;
                }
                else
                {
                    // If no model has been selected before, fallback to default behavior
                    m_currentModelName = std::nullopt;
                    m_currentVariantType.clear();
                    m_currentModelIndex = 0;
                }
            });
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
            else
            {
                return const_cast<ModelVariant *>(&model.quantized4Bit);
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

            m_downloadFutures.emplace_back(m_persistence->downloadModelVariant(*model, *variant));
        }

        bool useVulkanBackend() const
        {
            bool useVulkan = false;  // This will store our detection results

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
                -1,                          // COM negotiates services
                nullptr,                     // Authentication services
                nullptr,                     // Reserved
                RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication
                RPC_C_IMP_LEVEL_IMPERSONATE, // Default impersonation
                nullptr,                     // Authentication info
                EOAC_NONE,                   // Additional capabilities
                nullptr                      // Reserved
            );

            if (FAILED(hres))
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
                _bstr_t(L"ROOT\\CIMV2"), // WMI namespace
                nullptr,                 // User name
                nullptr,                 // Password
                nullptr,                 // Locale
                0,                       // Security flags
                nullptr,                 // Authority
                nullptr,                 // Context
                &pSvc                    // IWbemServices proxy
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
                RPC_C_AUTHN_WINNT,           // Authentication service
                RPC_C_AUTHZ_NONE,            // Authorization service
                nullptr,                     // Principal name
                RPC_C_AUTHN_LEVEL_CALL,      // Authentication level
                RPC_C_IMP_LEVEL_IMPERSONATE, // Impersonation level
                nullptr,                     // Client identity
                EOAC_NONE                    // Proxy capabilities
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
                bstr_t("SELECT * FROM Win32_VideoController"),
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
                    break;  // No more objects
                }

                // Get the AdapterCompatibility (or Name, or PNPDeviceID) property
                VARIANT vtProp;
                hr = pclsObj->Get(L"AdapterCompatibility", 0, &vtProp, 0, 0);
                if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr)
                {
                    std::wstring adapterName = vtProp.bstrVal;

                    // Check for NVIDIA
                    if (adapterName.find(L"NVIDIA") != std::wstring::npos)
                    {
                        useVulkan = true;
                        break;
                    }
                    // Check for AMD / ATI
                    if (adapterName.find(L"AMD") != std::wstring::npos ||
                        adapterName.find(L"ATI") != std::wstring::npos)
                    {
                        useVulkan = true;
                        break;
                    }
                }
                VariantClear(&vtProp);

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
            m_getInferenceEnginePtr = (GetInferenceEngineFunc*)
                GetProcAddress(m_inferenceLibHandle, "getInferenceEngine");
            if (!m_getInferenceEnginePtr) {
                std::cerr << "[ModelManager] Failed to get the address of getInferenceEngine from "
                    << backendName << std::endl;
                // Optionally FreeLibrary here if you want to clean up immediately
                return false;
            }
#endif
            return true;
        }

        bool loadModelIntoEngine()
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            // Check if we have a selected model
            if (!m_currentModelName.has_value()) {
                std::cerr << "[ModelManager] No current model selected.\n";
                return false;
            }

            // Retrieve the current variant
            ModelVariant* variant = getVariantLocked(m_currentModelIndex, m_currentVariantType);
            if (!variant) {
                std::cerr << "[ModelManager] Variant not found for current model.\n";
                return false;
            }

            // Check if downloaded
            if (!variant->isDownloaded) {
                std::cerr << "[ModelManager] Variant is not downloaded. Cannot load.\n";
                return false;
            }

            // Check if the file actually exists
            std::string modelPath = variant->path;
            if (!std::filesystem::exists(modelPath)) {
                std::cerr << "[ModelManager] Model file not found at: " << modelPath << "\n";
                return false;
            }

            // Make sure we have a valid function pointer
            if (!m_getInferenceEnginePtr) {
                std::cerr << "[ModelManager] No valid getInferenceEngine function pointer.\n";
                return false;
            }

			// TODO: Get the main GPU ID from the UI
            int mainGpuId = 0;

			// Get the model path directory instead of the full path file
			std::string modelDir = modelPath.substr(0, modelPath.find_last_of("/\\"));
			// Convert to absolute path
			modelDir = std::filesystem::absolute(modelDir).string();

			// Load the model into the inference engine
            InferenceEngine& engine = m_getInferenceEnginePtr();
            if (!engine.loadModel(modelDir.c_str(), mainGpuId))
			{
				std::cerr << "[ModelManager] Failed to load model into InferenceEngine: "
					<< modelDir << std::endl;
				return false;
			}

            std::cout << "[ModelManager] Successfully loaded model into InferenceEngine: "
                << modelDir << std::endl;

            return true;
        }

        mutable std::shared_mutex m_mutex;
        std::unique_ptr<IModelPersistence> m_persistence;
        std::vector<ModelData> m_models;
        std::unordered_map<std::string, size_t> m_modelNameToIndex;
        std::optional<std::string> m_currentModelName;
        std::string m_currentVariantType;
        size_t m_currentModelIndex;
        std::vector<std::future<void>> m_downloadFutures;

#ifdef _WIN32
        HMODULE m_inferenceLibHandle = nullptr;
#endif

        GetInferenceEngineFunc* m_getInferenceEnginePtr = nullptr;

		std::function<void(const std::string&, const int)> m_streamingCallback;
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