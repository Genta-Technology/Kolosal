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
        return Model::ModelManager::getInstance().isModelLoaded();
    }

    std::optional<std::string> getCurrentModelName() const {
        return Model::ModelManager::getInstance().getCurrentModelName();
    }

    // Model parameters change tracking
    bool haveModelParamsChanged() const { return m_modelParamsChanged; }
    void setModelParamsChanged(bool changed) { m_modelParamsChanged = changed; }
    void resetModelParamsChanged() { m_modelParamsChanged = false; }

private:
    ServerStateManager() : m_serverRunning(false), m_serverPort(8080), m_modelParamsChanged(false) {}

    bool m_serverRunning;
    int m_serverPort;
    bool m_modelParamsChanged;
};