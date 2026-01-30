#include <filesystem>
#include <chrono>
#include <thread>

#include "scripting/PythonScripting.hpp"

namespace fs = std::filesystem;

namespace PythonScripting {

ScriptHotReloader::ScriptHotReloader(const std::string& scriptDir, int checkIntervalMs)
    : scriptDir_(scriptDir), checkIntervalMs_(checkIntervalMs), running_(false) {
}

ScriptHotReloader::~ScriptHotReloader() {
    Stop();
}

void ScriptHotReloader::Start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    watchThread_ = std::thread(&ScriptHotReloader::WatchLoop, this);
}

void ScriptHotReloader::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    if (watchThread_.joinable()) {
        watchThread_.join();
    }
}

void ScriptHotReloader::AddModuleToWatch(const std::string& moduleName,
                                        const std::string& filePath) {
    std::lock_guard<std::mutex> lock(watchedModulesMutex_);
    
    if (fs::exists(filePath)) {
        watchedModules_[moduleName] = filePath;
        lastModified_[filePath] = fs::last_write_time(filePath);
        Logger::Info("Added module to watch: {} -> {}", moduleName, filePath);
    }
}

void ScriptHotReloader::RemoveModuleToWatch(const std::string& moduleName) {
    std::lock_guard<std::mutex> lock(watchedModulesMutex_);
    
    auto it = watchedModules_.find(moduleName);
    if (it != watchedModules_.end()) {
        std::string filePath = it->second;
        watchedModules_.erase(it);
        lastModified_.erase(filePath);
        Logger::Info("Removed module from watch: {}", moduleName);
    }
}

void ScriptHotReloader::WatchLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs_));
        
        std::lock_guard<std::mutex> lock(watchedModulesMutex_);
        
        for (const auto& [moduleName, filePath] : watchedModules_) {
            if (!fs::exists(filePath)) {
                continue;
            }
            
            auto currentTime = fs::last_write_time(filePath);
            auto it = lastModified_.find(filePath);
            
            if (it == lastModified_.end() || it->second != currentTime) {
                // File has been modified
                Logger::Info("Detected changes in module: {}", moduleName);
                
                // Reload the module
                auto& scripting = PythonScripting::GetInstance();
                if (scripting.ReloadModule(moduleName)) {
                    Logger::Info("Successfully reloaded module: {}", moduleName);
                } else {
                    Logger::Error("Failed to reload module: {}", moduleName);
                }
                
                // Update last modified time
                lastModified_[filePath] = currentTime;
            }
        }
        
        // Also watch for new Python files in the script directory
        if (fs::exists(scriptDir_)) {
            for (const auto& entry : fs::directory_iterator(scriptDir_)) {
                if (entry.path().extension() == ".py") {
                    std::string moduleName = entry.path().stem().string();
                    std::string filePath = entry.path().string();
                    
                    if (watchedModules_.find(moduleName) == watchedModules_.end()) {
                        // New module found
                        Logger::Info("Detected new module: {}", moduleName);
                        
                        // Add to watch list
                        watchedModules_[moduleName] = filePath;
                        lastModified_[filePath] = fs::last_write_time(filePath);
                        
                        // Try to load it
                        auto& scripting = PythonScripting::GetInstance();
                        if (!scripting.LoadModule(moduleName, filePath)) {
                            Logger::Warn("Failed to auto-load new module: {}", moduleName);
                        }
                    }
                }
            }
        }
    }
}

} // namespace PythonScripting