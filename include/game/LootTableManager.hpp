#pragma once

#include <algorithm>
#include <fstream>
#include <cmath>
#include <memory>
#include <mutex>
#include <random>
#include <vector>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "game/LootItem.hpp"
#include "game/LootTable.hpp"
#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"

class LootTableManager {
public:
    // Singleton pattern
    static LootTableManager& GetInstance();
    
    // Prevent copying and moving
    LootTableManager(const LootTableManager&) = delete;
    LootTableManager& operator=(const LootTableManager&) = delete;
    LootTableManager(LootTableManager&&) = delete;
    LootTableManager& operator=(LootTableManager&&) = delete;
    
    // Table management
    void RegisterTable(const LootTable& table);
    void UnregisterTable(const std::string& tableId);
    const LootTable* GetTable(const std::string& tableId) const;
    bool HasTable(const std::string& tableId) const;
    
    // Loot generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateLoot(
        const std::string& tableId,
        int playerLevel = 1,
        float luckMultiplier = 1.0f,
        const std::unordered_map<std::string, float>& factionRep = {}
    );
    
    // Multiple table generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateLootFromMultiple(
        const std::vector<std::string>& tableIds,
        int playerLevel = 1,
        float luckMultiplier = 1.0f
    );
    
    // Weighted loot generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateWeightedLoot(
        const std::vector<LootTable>& tables,
        const std::vector<float>& weights,
        int playerLevel = 1
    );
    
    // Gold generation
    int GenerateGold(const std::string& tableId, float luckMultiplier = 1.0f) const;
    int GenerateGold(const LootTable& table, float luckMultiplier = 1.0f) const;
    
    // Serialization
    bool LoadLootTables(const std::string& filePath);
    bool SaveLootTables(const std::string& filePath) const;
    bool LoadLootTablesFromJson(const nlohmann::json& jsonData);
    nlohmann::json SerializeAllTables() const;
    
    // Utility methods
    void ClearAllTables();
    size_t GetTableCount() const;
    
    // Requirement checking
    static bool PlayerMeetsRequirements(
        const LootEntry& entry,
        int playerLevel,
        const std::unordered_map<std::string, float>& factionRep
    );
    
private:
    LootTableManager();
    ~LootTableManager() = default;
    
    // Helper methods for loot generation
    std::shared_ptr<LootItem> CreateItemFromEntry(
        const LootEntry& entry,
        int playerLevel,
        float luckMultiplier
    ) const;
    
    LootRarity GenerateRarity(
        LootRarity minRarity,
        LootRarity maxRarity,
        float luckMultiplier
    ) const;
    
    float CalculateAdjustedDropChance(
        float baseChance,
        float luckMultiplier,
        int playerLevel,
        int itemLevel
    ) const;
    
    // Item generation helpers
    void ApplyRarityStats(std::shared_ptr<LootItem> item, LootRarity rarity) const;
    void GenerateRandomStats(std::shared_ptr<LootItem> item, int itemLevel) const;
    void ApplyRandomEnchantment(std::shared_ptr<LootItem> item, LootRarity rarity) const;
    
    // Random number generation
    float GetRandomFloat(float min = 0.0f, float max = 1.0f) const;
    int GetRandomInt(int min, int max) const;
    
    // Data storage
    std::unordered_map<std::string, LootTable> lootTables_;
    
    // Random number generator
    mutable std::mt19937 rng_;
    mutable std::mutex rngMutex_;
    
    // Thread safety
    mutable std::mutex tablesMutex_;
    
    // Singleton instance
    static std::unique_ptr<LootTableManager> instance_;
    static std::once_flag initFlag_;
};
