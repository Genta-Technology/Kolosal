#pragma once

#include "model_manager.hpp"

#include <string>
#include <functional>
#include <optional>

class ServerStateManager {
public:
    static ServerStateManager& getInstance() {
        static ServerStateManager instance;
        return instance;
    }

    // Server status
    bool isServerRunning() const { return m_serverRunning; }
    void setServerRunning(bool running) { m_serverRunning = running; }

    // Server port
    int getServerPort() const { return m_serverPort; }
    void setServerPort(int port) { m_serverPort = port; }

    // Get port as string for display and connection purposes
    std::string getServerPortString() const {
        return std::to_string(m_serverPort);
    }

    // Model state observers
    bool isModelLoadInProgress() const {
        return Model::ModelManager::getInstance().isLoadInProgress();
    }

    bool isModelLoaded() const {
        return !Model::ModelManager::getInstance().getModelNamesInServer().empty();
    }

    std::optional<std::string> getCurrentModelName() const {
        return Model::ModelManager::getInstance().getCurrentModelName();
    }

    // Model parameters change tracking
    bool haveModelParamsChanged(const std::string modelId) const { 
		return std::find(m_modelNeedsReload.begin(), m_modelNeedsReload.end(), modelId) != m_modelNeedsReload.end();
    }

    void setModelParamsChanged() { 
		auto& modelManager = Model::ModelManager::getInstance();
		auto models = modelManager.getModelIds();
		for (const auto& modelData : models) {
			m_modelNeedsReload.push_back(modelData);
		}
    }

    void resetModelParamsChanged(const std::string modelId) { 
		if (m_modelNeedsReload.size() > 0) {
			auto it = std::remove(m_modelNeedsReload.begin(), m_modelNeedsReload.end(), modelId);
			m_modelNeedsReload.erase(it, m_modelNeedsReload.end());
		}
    }

private:
    ServerStateManager() : m_serverRunning(false), m_serverPort(8080) {}

	std::vector<std::string> m_modelNeedsReload;

    bool m_serverRunning;
    int  m_serverPort;
};