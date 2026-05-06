#include "scripting/PythonScripting.hpp"

namespace fs = std::filesystem;

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

std::string PythonScripting::GetCurrentDirectory() {
    return std::filesystem::current_path().string();
}

bool PythonScripting::CheckStatus(PyStatus status)
{
    if (PyStatus_IsError(status))
    {
        Logger::Error("status.exitcode = {} ({})", status.exitcode, status.err_msg ? status.err_msg : "unknown error");
        return false;
    }
    else if (PyStatus_Exception(status))
    {
        Logger::Critical("status.exitcode = {} ({})", status.exitcode, status.err_msg ? status.err_msg : "unknown error");
        //Py_ExitStatusException(status);
        return false;
    }
    else if (PyStatus_IsExit(status))
    {
        Logger::Error("status.exitcode = {} ({})", status.exitcode, status.err_msg ? status.err_msg : "unknown error");
        return false;
    }
    return true;
}

void PythonScripting::CheckStatusConf(PyStatus status, PyConfig pyconf)
{
    if (!CheckStatus(status)) {
        PyConfig_Clear(&pyconf);
        initialized_ = false;
    }
}

bool PythonScripting::Initialize() {
    std::string curdir = GetCurrentDirectory();
    auto& config = ConfigManager::GetInstance();
    std::string home = config.GetString("scripting.python.home", "scripts/python", true);
    std::string platlibdir = config.GetString("scripting.python.platlibdir", "lib", true);
    std::string module_search_paths = config.GetString("scripting.python.module_search_paths", "/usr/lib/python;/usr/local/lib/python");
    WArray module_search_paths_array(module_search_paths, ";", curdir+"/");
    PyStatus status;
    PyConfig pyconf;
    pyconf.isolated = config.GetInt("scripting.python.isolated", 1, true);
    if (pyconf.isolated == 1) {
        PyConfig_InitIsolatedConfig(&pyconf);
    }
    else
    {
        PyConfig_InitPythonConfig(&pyconf);
    }
    pyconf.site_import = config.GetInt("scripting.python.site_import", 1);
    pyconf.user_site_directory = config.GetInt("scripting.python.user_site_directory", 1);
    initialized_ = true;//set before first CheckStatusConf call
    status = PyConfig_SetBytesString(&pyconf, &pyconf.program_name, config.GetString("scripting.python.program_name", "gameserver").c_str());
    CheckStatusConf(status, pyconf);
    if (!home.empty()) {
        status = PyConfig_SetBytesString(&pyconf, &pyconf.home, (curdir+"/"+home).c_str());
        CheckStatusConf(status, pyconf);
    }
    if (!module_search_paths.empty()) {
        pyconf.module_search_paths_set = 1;
        for (size_t i = 0; i < module_search_paths_array.size(); ++i) {
            wchar_t** paths = module_search_paths_array.get();
            //Logger::Trace("PYTHON.MODULE_SEARCH_PATHS: {} ({})", i, wstring_to_string(std::wstring(paths[i])));
            status = PyWideStringList_Append(&pyconf.module_search_paths, paths[i]);
            if (!CheckStatus(status)) {
                PyConfig_Clear(&pyconf);
                initialized_ = false;
                break;
            }
        }
    }
    status = PyConfig_SetBytesString(&pyconf, &pyconf.platlibdir, (curdir+"/"+home+"/"+platlibdir).c_str());
    CheckStatusConf(status, pyconf);
    status = PyConfig_SetBytesString(&pyconf, &pyconf.prefix, curdir.c_str());
    CheckStatusConf(status, pyconf);

    status = Py_InitializeFromConfig(&pyconf);
    CheckStatusConf(status, pyconf);
    if (!initialized_)
    {
        ShutdownPython();
    }
    else
        Logger::Info("Python interpreter ready (version: {})", Py_GetVersion());
    return initialized_;
}

bool PythonScripting::IsInitialized() const { return initialized_; }

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

void PythonScripting::ShutdownPython() {
    try {
        Py_FinalizeEx();
        Logger::Debug("Python interpreter finalized");
    } catch (const std::exception& err) {
        Logger::Error("Exception finalizing Python: {}", err.what());
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
    {
        std::shared_lock<std::shared_mutex> modulesLock(modulesMutex_);
        auto it = modules_.find(moduleName);
        if (it == modules_.end()) {
            Logger::Error("PythonScripting::RegisterEventHandler: module not found: {}", moduleName);
            return;
        }
        if (!it->second->HasFunction(functionName)) {
            Logger::Error("PythonScripting::RegisterEventHandler: function not found: {}.{}",
                         moduleName, functionName);
            return;
        }
    }
    eventHandlers_[eventName].emplace_back(moduleName, functionName);
    Logger::Trace("PythonScripting::RegisterEventHandler: {} -> {}.{}",
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
