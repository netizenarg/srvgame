#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <uuid/uuid.h>

#include <nlohmann/json.hpp>
#include <Python.h>

#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
//#include "database/DbManager.hpp"
// #include "network/ConnectionManager.hpp"
#include "game/PlayerManager.hpp"
//#include "game/GameLogic.hpp"

class PythonAPI {

public:
    // =============== Python C API Functions ===============
    
    // Initialize Python API
    static void Initialize();
    
    // Logging functions
    static void LogDebug(const std::string& message);
    static void LogInfo(const std::string& message);
    static void LogWarning(const std::string& message);
    static void LogError(const std::string& message);
    static void LogCritical(const std::string& message);
    
    // Player functions
    static nlohmann::json GetPlayer(int64_t playerId);
    static bool SetPlayerPosition(int64_t playerId, float x, float y, float z);
    static bool GivePlayerItem(int64_t playerId, const std::string& itemId, int count);
    static bool TakePlayerItem(int64_t playerId, const std::string& itemId, int count);
    static bool AddPlayerExperience(int64_t playerId, int64_t amount);
    static bool SetPlayerHealth(int64_t playerId, int health);
    static bool SetPlayerMana(int64_t playerId, int mana);
    static bool TeleportPlayer(int64_t playerId, float x, float y, float z);
    static bool SendMessageToPlayer(int64_t playerId, const std::string& message);
    static bool BroadcastToNearby(int64_t playerId, const std::string& message, float radius);
    
    // Database functions with parameterized query support
    static nlohmann::json QueryDatabase(const std::string& query);
    static nlohmann::json QueryDatabase(const std::string& query, const std::vector<std::string>& params);
    static bool ExecuteDatabase(const std::string& query);
    static bool ExecuteDatabase(const std::string& query, const std::vector<std::string>& params);
    static nlohmann::json GetPlayerFromDB(int64_t playerId);
    static bool SavePlayerToDB(int64_t playerId, const nlohmann::json& data);
    
    // Event functions
    static void FireEvent(const std::string& eventName, const nlohmann::json& data);
    static void ScheduleEvent(int delayMs, const std::string& eventName, const nlohmann::json& data);
    
    // Utility functions
    static int64_t GetCurrentTime();
    static std::string GenerateUUID();
    static nlohmann::json ParseJSON(const std::string& jsonStr);
    static std::string StringifyJSON(const nlohmann::json& json);
    
    // Math functions
    static float RandomFloat(float min, float max);
    static int RandomInt(int min, int max);
    static float Distance(float x1, float y1, float z1, float x2, float y2, float z2);
    
    // Configuration
    static nlohmann::json GetConfig(const std::string& key);
    static bool SetConfig(const std::string& key, const nlohmann::json& value);
};

// Helper functions for JSON-Python conversion
nlohmann::json PythonToJson(PyObject* obj);
PyObject* JsonToPython(const nlohmann::json& json);
std::vector<std::string> PythonSequenceToStringVector(PyObject* obj);
