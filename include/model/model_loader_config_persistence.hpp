#ifndef MODEL_LOADER_CONFIG_PERSISTENCE_HPP
#define MODEL_LOADER_CONFIG_PERSISTENCE_HPP

#include <string>
#include <json.hpp>
#include <types.h>

namespace Model
{
    class ModelLoaderConfigPersistence {
    public:
        /**
         * @brief Save configuration to a JSON file
         * @param config The model loader configuration
         * @param filePath Path to save the configuration
         * @return true if successful, false otherwise
         */
        bool saveToFile(const LoadingParameters& config, const std::string& filePath) {
            try {
                nlohmann::json j = configToJson(config);

                std::ofstream file(filePath);
                if (!file.is_open()) {
                    std::cerr << "Error: Could not open file for writing: " << filePath << std::endl;
                    return false;
                }

                file << j.dump(4); // Pretty print with 4 spaces indentation
                file.close();

                return true;
            }
            catch (const std::exception& e) {
                std::cerr << "Error saving configuration: " << e.what() << std::endl;
                return false;
            }
        }

        /**
         * @brief Load configuration from a JSON file
         * @param filePath Path to the configuration file
         * @param config The configuration to populate
         * @return true if successful, false otherwise
         */
        bool loadFromFile(const std::string& filePath, LoadingParameters& config) {
            try {
                std::ifstream file(filePath);
                if (!file.is_open()) {
                    std::cerr << "Error: Could not open file for reading: " << filePath << std::endl;
                    return false;
                }

                nlohmann::json j;
                file >> j;
                file.close();

                jsonToConfig(j, config);
                return true;
            }
            catch (const std::exception& e) {
                std::cerr << "Error loading configuration: " << e.what() << std::endl;
                return false;
            }
        }

    private:
        nlohmann::json configToJson(const LoadingParameters& config) {
            nlohmann::json j;

            j["n_ctx"] = config.n_ctx;
            j["n_keep"] = config.n_keep;
            j["use_mlock"] = config.use_mlock;
            j["use_mmap"] = config.use_mmap;
            j["cont_batching"] = config.cont_batching;
            j["warmup"] = config.warmup;
            j["n_parallel"] = config.n_parallel;
            j["n_gpu_layers"] = config.n_gpu_layers;

            return j;
        }

        void jsonToConfig(const nlohmann::json& json, LoadingParameters& config) {
            if (json.contains("n_ctx")) config.n_ctx = json["n_ctx"];
            if (json.contains("n_keep")) config.n_keep = json["n_keep"];
            if (json.contains("use_mlock")) config.use_mlock = json["use_mlock"];
            if (json.contains("use_mmap")) config.use_mmap = json["use_mmap"];
            if (json.contains("cont_batching")) config.cont_batching = json["cont_batching"];
            if (json.contains("warmup")) config.warmup = json["warmup"];
            if (json.contains("n_parallel")) config.n_parallel = json["n_parallel"];
            if (json.contains("n_gpu_layers")) config.n_gpu_layers = json["n_gpu_layers"];
        }
    };
} // namespace Model

#endif // MODEL_LOADER_CONFIG_PERSISTENCE_HPP