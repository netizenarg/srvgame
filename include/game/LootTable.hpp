#pragma once

#include <algorithm>
#include <fstream>
#include <random>
#include <vector>
#include <unordered_map>

#include "logging/Logger.hpp"
#include "game/LootItem.hpp"

struct LootEntry {
    uint64_t itemId;
    std::string name;
    float dropChance = 0.0f;  // 0.0 to 1.0
    int minQuantity = 1;
    int maxQuantity = 1;
    int minLevel = 1;
    int maxLevel = 100;
    LootRarity minRarity = LootRarity::COMMON;
    LootRarity maxRarity = LootRarity::MYTHIC;
    std::string requiredQuest;
    std::string requiredFaction;
    float factionRepRequired = 0.0f;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct LootTable {
    std::string tableId;
    std::string name;
    std::vector<LootEntry> entries;
    int guaranteedDrops = 0;  // Number of guaranteed drops
    int maxDrops = 5;  // Maximum number of items that can drop
    bool uniqueDrops = false;  // If true, items won't drop more than once per roll
    float goldMultiplier = 1.0f;
    int minGold = 0;
    int maxGold = 0;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};
