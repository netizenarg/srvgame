#include "scripting/PythonScripting.hpp"

namespace fs = std::filesystem;

// =============== PythonScripting Implementation ===============

PythonScripting& PythonScripting::GetInstance() {
    static PythonScripting instance;
    return instance;
}

PythonScripting::PythonScripting()
    : initialized_(false) {
}

PythonScripting::~PythonScripting() {
    Shutdown();
}

bool PythonScripting::Initialize() {
    if (initialized_) {
        Logger::Warn("PythonScripting already initialized");
        return true;
    }

    Logger::Info("Initializing Python scripting engine...");

    // Get configuration
    auto& config = ConfigManager::GetInstance();
    std::string pythonHome = config.GetString("scripting.python.directory", "");

    if (!pythonHome.empty()) {
        pythonHome_ = pythonHome;
    }

    // Initialize Python
    if (!InitializePython()) {
        Logger::Warn("Failed to initialize Python interpreter");
        return false;
    }

    // Set Python paths
    std::vector<std::string> paths = {
        "./scripts",
        "./python",
        "./lib/python",
        "."
    };

    for (const auto& path : paths) {
        AddPythonPath(path);
    }

    // Add additional paths from config
    auto pythonPaths = config.GetStringArray("scripting.python.paths");
    for (const auto& path : pythonPaths) {
        AddPythonPath(path);
    }

    // Initialize Python API
    PythonAPI::Initialize();

    // Load default modules
    std::string scriptDir = config.GetString("scripting.python.directory", "./scripts");
    if (fs::exists(scriptDir)) {
        for (const auto& entry : fs::directory_iterator(scriptDir)) {
            if (entry.path().extension() == ".py") {
                std::string moduleName = entry.path().stem().string();
                std::string filePath = entry.path().string();

                if (LoadModule(moduleName, filePath)) {
                    Logger::Info("Loaded Python module: {}", moduleName);
                } else {
                    Logger::Warn("Failed to load Python module: {}", moduleName);
                }
            }
        }
    }

    initialized_ = true;
    Logger::Info("Python scripting engine initialized successfully");
    return true;
}

void PythonScripting::Shutdown() {
    if (!initialized_) {
        return;
    }

    Logger::Info("Shutting down Python scripting engine...");

    {
        std::unique_lock<std::shared_mutex> lock(modulesMutex_);
        modules_.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);
        eventHandlers_.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
        callbacks_.clear();
    }

    ShutdownPython();

    initialized_ = false;
    Logger::Info("Python scripting engine shutdown complete");
}

bool PythonScripting::InitializePython() {
    try {
        PyConfig config;
        PyConfig_InitPythonConfig(&config);

        // Set Python home if specified
        if (!pythonHome_.empty()) {
            //Py_SetPythonHome(Py_DecodeLocale(pythonHome_.c_str(), nullptr));
            config.home = Py_DecodeLocale(pythonHome_.c_str(), nullptr);
        }

        // Initialize Python
        //Py_Initialize();
        Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);

        if (!Py_IsInitialized()) {
            Logger::Warn("Failed to initialize Python interpreter");
            return false;
        }

        // Initialize threads
        //PyEval_InitThreads(); //Py_DEPRECATED(3.9) PyAPI_FUNC(void)

        // Release GIL - we'll acquire it when needed
        PyThreadState* mainThread = PyEval_SaveThread();
        if (!mainThread) {
            Logger::Error("Failed to save Python thread state");
            return false;
        }

        // Import essential modules
        PyGILGuard gil;

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        PyRun_SimpleString("import json");
        PyRun_SimpleString("import math");
        PyRun_SimpleString("import random");
        PyRun_SimpleString("import time");

        Logger::Debug("Python interpreter initialized (version: {})", Py_GetVersion());
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Exception initializing Python: {}", e.what());
        return false;
    }
}

void PythonScripting::ShutdownPython() {
    try {
        PyGILGuard gil;
        Py_FinalizeEx();
        Logger::Debug("Python interpreter finalized");
    } catch (const std::exception& e) {
        Logger::Error("Exception finalizing Python: {}", e.what());
    }
}

bool PythonScripting::AddPythonPath(const std::string& path) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    PyGILGuard gil;

    try {
        std::string code = "import sys\n";
        code += "if '" + path + "' not in sys.path:\n";
        code += "    sys.path.insert(0, '" + path + "')\n";

        PyRun_SimpleString(code.c_str());

        pythonPaths_.push_back(path);
        Logger::Debug("Added Python path: {}", path);
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to add Python path {}: {}", path, e.what());
        return false;
    }
}

bool PythonScripting::ExecuteString(const std::string& code) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    PyGILGuard gil;

    try {
        int result = PyRun_SimpleString(code.c_str());
        if (result != 0) {
            Logger::Error("Python execution failed for code: {}", code);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Exception executing Python code: {}", e.what());
        return false;
    }
}

bool PythonScripting::LoadModule(const std::string& name, const std::string& path) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(modulesMutex_);
        if (modules_.find(name) != modules_.end()) {
            Logger::Warn("Module already loaded: {}", name);
            return true;
        }

        auto module = std::make_unique<PythonModule>(name, path);
        if (!module->Load()) {
            Logger::Error("Failed to load Python module {}: {}", name, module->GetLastError());
            return false;
        }

        modules_[name] = std::move(module);
    }

    Logger::Info("Python module loaded: {}", name);
    return true;
}

bool PythonScripting::UnloadModule(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(name);
    if (it == modules_.end()) {
        Logger::Warn("Module not found: {}", name);
        return false;
    }

    // Remove any event handlers for this module
    {
        std::unique_lock<std::shared_mutex> eventLock(eventHandlersMutex_);
        for (auto& [eventName, handlers] : eventHandlers_) {
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [&name](const auto& handler) {
                        return handler.first == name;
                    }),
                handlers.end()
            );
        }
    }

    it->second->Unload();
    modules_.erase(it);

    Logger::Info("Python module unloaded: {}", name);
    return true;
}

bool PythonScripting::ReloadModule(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(name);
    if (it == modules_.end()) {
        Logger::Warn("Module not found: {}", name);
        return false;
    }

    if (!it->second->Reload()) {
        Logger::Error("Failed to reload Python module {}: {}", name, it->second->GetLastError());
        return false;
    }

    Logger::Info("Python module reloaded: {}", name);
    return true;
}

void PythonScripting::RegisterEventHandler(const std::string& eventName,
                                         const std::string& moduleName,
                                         const std::string& functionName) {
    std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);

    // Check if module exists
    {
        std::shared_lock<std::shared_mutex> modulesLock(modulesMutex_);
        auto it = modules_.find(moduleName);
        if (it == modules_.end()) {
            Logger::Error("Cannot register event handler: module not found: {}", moduleName);
            return;
        }

        if (!it->second->HasFunction(functionName)) {
            Logger::Error("Cannot register event handler: function not found: {}.{}",
                         moduleName, functionName);
            return;
        }
    }

    eventHandlers_[eventName].emplace_back(moduleName, functionName);
    Logger::Debug("Registered event handler: {} -> {}.{}",
                 eventName, moduleName, functionName);
}

void PythonScripting::UnregisterEventHandler(const std::string& eventName,
                                           const std::string& moduleName) {
    std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);

    auto it = eventHandlers_.find(eventName);
    if (it == eventHandlers_.end()) {
        return;
    }

    auto& handlers = it->second;
    handlers.erase(
        std::remove_if(handlers.begin(), handlers.end(),
            [&moduleName](const auto& handler) {
                return handler.first == moduleName;
            }),
        handlers.end()
    );

    if (handlers.empty()) {
        eventHandlers_.erase(it);
    }

    Logger::Debug("Unregistered event handlers for {} from module {}",
                 eventName, moduleName);
}

bool PythonScripting::FireEvent(const GameEvent& event) {
    return FireEvent(event.name, event.ToJson());
}

bool PythonScripting::FireEvent(const std::string& eventName, const nlohmann::json& data) {
    if (!initialized_) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> handlers;

    {
        std::shared_lock<std::shared_mutex> lock(eventHandlersMutex_);
        auto it = eventHandlers_.find(eventName);
        if (it == eventHandlers_.end()) {
            return false;
        }
        handlers = it->second;
    }

    if (handlers.empty()) {
        return false;
    }

    bool anySuccess = false;
    for (const auto& [moduleName, functionName] : handlers) {
        std::shared_lock<std::shared_mutex> modulesLock(modulesMutex_);

        auto moduleIt = modules_.find(moduleName);
        if (moduleIt == modules_.end()) {
            Logger::Warn("Module not found for event handler: {}", moduleName);
            continue;
        }

        nlohmann::json eventData = {
            {"event", eventName},
            {"data", data},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        if (moduleIt->second->CallFunction(functionName, eventData)) {
            anySuccess = true;
        } else {
            Logger::Warn("Event handler failed: {}.{} for event {}",
                        moduleName, functionName, eventName);
        }
    }

    return anySuccess;
}

bool PythonScripting::CallFunction(const std::string& moduleName,
                                 const std::string& functionName,
                                 const nlohmann::json& args) {
    if (!initialized_) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        Logger::Error("Module not found: {}", moduleName);
        return false;
    }

    return it->second->CallFunction(functionName, args);
}

nlohmann::json PythonScripting::CallFunctionWithResult(const std::string& moduleName,
                                                     const std::string& functionName,
                                                     const nlohmann::json& args) {
    if (!initialized_) {
        return nlohmann::json();
    }

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        Logger::Error("Module not found: {}", moduleName);
        return nlohmann::json();
    }

    return it->second->CallFunctionWithResult(functionName, args);
}

void PythonScripting::RegisterCallback(const std::string& callbackName, PyCallback callback) {
    std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
    callbacks_[callbackName] = std::move(callback);
    Logger::Debug("Registered Python callback: {}", callbackName);
}

void PythonScripting::UnregisterCallback(const std::string& callbackName) {
    std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
    callbacks_.erase(callbackName);
    Logger::Debug("Unregistered Python callback: {}", callbackName);
}

bool PythonScripting::HasCallback(const std::string& callbackName) const {
    std::shared_lock<std::shared_mutex> lock(callbacksMutex_);
    return callbacks_.find(callbackName) != callbacks_.end();
}

std::vector<std::string> PythonScripting::GetLoadedModules() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);
    for (const auto& [name, module] : modules_) {
        result.push_back(name);
    }

    return result;
}

std::vector<std::string> PythonScripting::GetRegisteredEvents() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(eventHandlersMutex_);
    for (const auto& [eventName, handlers] : eventHandlers_) {
        result.push_back(eventName);
    }

    return result;
}

std::vector<std::string> PythonScripting::GetRegisteredCallbacks() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(callbacksMutex_);
    for (const auto& [callbackName, callback] : callbacks_) {
        result.push_back(callbackName);
    }

    return result;
}
