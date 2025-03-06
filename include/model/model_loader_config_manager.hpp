#ifndef MODEL_LOADER_CONFIG_MANAGER_HPP
#define MODEL_LOADER_CONFIG_MANAGER_HPP

#include "model_loader_config_persistence.hpp"

#include <string>
#include <json.hpp>
#include <types.h>
#include <iostream>

namespace Model
{
    /**
     * @brief Class for managing LLM model loading configuration
     */
    class ModelLoaderConfigManager {
    public:
        /**
         * @brief Get singleton instance of config manager
         * @param configFilePath Path to the configuration file (optional on first call)
         * @return Reference to the singleton instance
         */
        static ModelLoaderConfigManager& getInstance(const std::string& configFilePath = "")
        {
            static ModelLoaderConfigManager instance(configFilePath.empty() ? "model_config.json" : configFilePath);

            if (!configFilePath.empty() && configFilePath != instance.configFilePath_) {
                // Log a warning that the config file path is being ignored after initialization
                std::cerr << "Warning: Config file path '" << configFilePath
                    << "' is ignored as the instance is already initialized with '"
                    << instance.configFilePath_ << "'" << std::endl;
            }

            return instance;
        }

        // Delete copy constructor and assignment operator
        ModelLoaderConfigManager(const ModelLoaderConfigManager&) = delete;
        ModelLoaderConfigManager& operator=(const ModelLoaderConfigManager&) = delete;

        /**
         * @brief Get the current configuration
         * @return Reference to the current configuration
         */
        const LoadingParameters& getConfig() const {
            return config_;
        }

        /**
         * @brief Set a complete new configuration
         * @param config The new configuration
         */
        void setConfig(const LoadingParameters& config) {
            config_ = config;
        }

        /**
         * @brief Save current configuration to disk
         * @return true if successful, false otherwise
         */
        bool saveConfig() {
            return persistence_.saveToFile(config_, configFilePath_);
        }

        /**
         * @brief Load configuration from disk
         * @return true if successful, false otherwise
         */
        bool loadConfig() {
            return persistence_.loadFromFile(configFilePath_, config_);
        }

        // Getters
        int getContextSize() const { return config_.n_ctx; }
        int getKeepSize() const { return config_.n_keep; }
        bool getUseMlock() const { return config_.use_mlock; }
        bool getUseMmap() const { return config_.use_mmap; }
        bool getContinuousBatching() const { return config_.cont_batching; }
        bool getWarmup() const { return config_.warmup; }
        int getParallelCount() const { return config_.n_parallel; }
        int getGpuLayers() const { return config_.n_gpu_layers; }

        // Setters
        void setContextSize(int size) { config_.n_ctx = size; }
        void setKeepSize(int size) { config_.n_keep = size; }
        void setUseMlock(bool use) { config_.use_mlock = use; }
        void setUseMmap(bool use) { config_.use_mmap = use; }
        void setContinuousBatching(bool enable) { config_.cont_batching = enable; }
        void setWarmup(bool enable) { config_.warmup = enable; }
        void setParallelCount(int count) { config_.n_parallel = count; }
        void setGpuLayers(int layers) { config_.n_gpu_layers = layers; }

    private:
        explicit ModelLoaderConfigManager(const std::string& configFilePath)
            : configFilePath_(configFilePath) {
            // Try loading from file, if it fails, use default values
            if (!loadConfig()) {
                std::cout << "Using default configuration values" << std::endl;
            }
        }

        LoadingParameters config_;
        std::string configFilePath_;
        ModelLoaderConfigPersistence persistence_;
    };

	inline void initializeModelLoaderConfigManager(const std::string& configFilePath = "") {
		ModelLoaderConfigManager::getInstance(configFilePath);
	}

} // namespace Model

#endif // MODEL_LOADER_CONFIG_MANAGER_HPP