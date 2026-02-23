#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "scripting/PythonAPI.hpp"
#include "scripting/PythonModule.hpp"

// Forward declarations
//class Player;
//class GameSession;


// Python object wrapper for RAII
class PyObjectRef {
public:
    PyObjectRef(PyObject* obj = nullptr) : obj_(obj) {}
    ~PyObjectRef() { if (obj_) Py_DECREF(obj_); }

    PyObject* get() { return obj_; }
    PyObject* release() { PyObject* temp = obj_; obj_ = nullptr; return temp; }
    PyObject* operator->() { return obj_; }
    operator bool() const { return obj_ != nullptr; }

private:
    PyObject* obj_;
};

// Event types that can be handled by Python
enum class EventType {
    PLAYER_LOGIN,
    PLAYER_LOGOUT,
    PLAYER_MOVE,
    PLAYER_ATTACK,
    PLAYER_DAMAGE,
    PLAYER_HEAL,
    PLAYER_LEVEL_UP,
    PLAYER_QUEST_ACCEPT,
    PLAYER_QUEST_COMPLETE,
    PLAYER_ITEM_ACQUIRE,
    PLAYER_ITEM_USE,
    PLAYER_CHAT,
    PLAYER_DEATH,
    PLAYER_RESPAWN,
    NPC_SPAWN,
    NPC_DESPAWN,
    NPC_AI_TICK,
    COMBAT_START,
    COMBAT_END,
    ZONE_ENTER,
    ZONE_EXIT,
    TRADE_START,
    TRADE_COMPLETE,
    GUILD_CREATE,
    GUILD_JOIN,
    GUILD_LEAVE,
    ACHIEVEMENT_EARNED,
    CUSTOM_EVENT
};

// Event data structure
struct GameEvent {
    EventType type;
    std::string name;
    nlohmann::json data;
    int64_t timestamp;
    uint64_t session_id;
    int64_t player_id;
    std::string source;

    GameEvent(EventType t, const std::string& n, const nlohmann::json& d = {})
    : type(t), name(n), data(d), timestamp(0), session_id(0), player_id(0) {
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    nlohmann::json ToJson() const {
        return {
            {"type", static_cast<int>(type)},
            {"name", name},
            {"data", data},
            {"timestamp", timestamp},
            {"session_id", session_id},
            {"player_id", player_id},
            {"source", source}
        };
    }
};

// Callback from Python to C++
using PyCallback = std::function<void(const nlohmann::json&)>;


// Main Python scripting engine
class PythonScripting {
public:
    static PythonScripting& GetInstance();

    bool Initialize();
    void Shutdown();

    // Module management
    bool LoadModule(const std::string& name, const std::string& path);
    bool UnloadModule(const std::string& name);
    bool ReloadModule(const std::string& name);

    // Event handling
    void RegisterEventHandler(const std::string& eventName,
                                const std::string& moduleName,
                                const std::string& functionName);
    void UnregisterEventHandler(const std::string& eventName,
                                const std::string& moduleName);

    bool FireEvent(const GameEvent& event);
    bool FireEvent(const std::string& eventName, const nlohmann::json& data = {});

    // Direct function calls
    bool CallFunction(const std::string& moduleName,
                        const std::string& functionName,
                        const nlohmann::json& args = {});

    nlohmann::json CallFunctionWithResult(const std::string& moduleName,
                                            const std::string& functionName,
                                            const nlohmann::json& args = {});

    // Callback registration (Python -> C++)
    void RegisterCallback(const std::string& callbackName, PyCallback callback);
    void UnregisterCallback(const std::string& callbackName);
    bool HasCallback(const std::string& callbackName) const;

    // Utility methods
    std::vector<std::string> GetLoadedModules() const;
    std::vector<std::string> GetRegisteredEvents() const;
    std::vector<std::string> GetRegisteredCallbacks() const;

    bool IsInitialized() const { return initialized_; }

private:
    PythonScripting();
    ~PythonScripting();

    bool InitializePython();
    void ShutdownPython();
    bool AddPythonPath(const std::string& path);
    bool ExecuteString(const std::string& code);

    // Thread-safe containers
    mutable std::shared_mutex modulesMutex_;
    std::unordered_map<std::string, std::unique_ptr<PythonModule>> modules_;

    mutable std::shared_mutex eventHandlersMutex_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> eventHandlers_;

    mutable std::shared_mutex callbacksMutex_;
    std::unordered_map<std::string, PyCallback> callbacks_;

    bool initialized_;
    std::string pythonHome_;
    std::vector<std::string> pythonPaths_;

    // Singleton
    PythonScripting(const PythonScripting&) = delete;
    PythonScripting& operator=(const PythonScripting&) = delete;
};

// Helper class for Python script hot-reloading
class ScriptHotReloader {
public:
    ScriptHotReloader(const std::string& scriptDir, int checkIntervalMs = 1000);
    ~ScriptHotReloader();

    void Start();
    void Stop();
    void AddModuleToWatch(const std::string& moduleName, const std::string& filePath);
    void RemoveModuleToWatch(const std::string& moduleName);

private:
    void WatchLoop();

    std::string scriptDir_;
    int checkIntervalMs_;
    std::atomic<bool> running_;
    std::thread watchThread_;

    mutable std::mutex watchedModulesMutex_;
    std::unordered_map<std::string, std::string> watchedModules_;
    std::unordered_map<std::string, std::filesystem::file_time_type> lastModified_;
};
