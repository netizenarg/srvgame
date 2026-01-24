#include <memory>

#include "database/CitusClient.hpp"
#include "database/Backend.hpp"
#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"

// =============== Static Members ===============
std::mutex CitusClient::instanceMutex_;
CitusClient* CitusClient::instance_ = nullptr;

// =============== Singleton Access ===============
CitusClient& CitusClient::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new CitusClient();
    }
    return *instance_;
}

// =============== Initialization ===============
bool CitusClient::Initialize(const std::string& coordinatorConnInfo,
                            const std::vector<std::string>& workerNodes) {
    auto& config = ConfigManager::GetInstance();
    
    // Determine backend type
    std::string backendType = config.GetString("database.backend", "postgresql");
    
    // Check if we should use Citus emulation
    bool useCitusEmulation = config.GetBool("database.useCitusEmulation", true);
    
    if (backendType == "postgresql" && useCitusEmulation) {
        Logger::Info("Using PostgreSQL backend with Citus emulation");
    } else if (backendType == "citus") {
        Logger::Info("Using Citus backend");
    } else {
        Logger::Info("Using PostgreSQL backend");
    }
    
    // Create the appropriate backend
    backend_ = CreateDatabaseBackend(backendType);
    if (!backend_) {
        Logger::Critical("Failed to create database backend: {}", backendType);
        return false;
    }
    
    // Initialize the backend
    if (!backend_->Initialize(coordinatorConnInfo, workerNodes)) {
        Logger::Critical("Failed to initialize database backend");
        return false;
    }
    
    backendType_ = backendType;
    Logger::Info("CitusClient initialized with {} backend", backendType_);
    return true;
}

// =============== Utility Method ===============
bool CitusClient::IsOnline(int64_t playerId) {
    auto player = GetPlayer(playerId);
    if (player.empty()) {
        return false;
    }
    
    return player.value("online", false);
}
