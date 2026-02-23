#include "game/LootTableManager.hpp"

// Initialize static members
std::unique_ptr<LootTableManager> LootTableManager::instance_ = nullptr;
std::once_flag LootTableManager::initFlag_;

LootTableManager::LootTableManager() {
    // Seed the random number generator
    std::random_device rd;
    rng_.seed(rd());
}

LootTableManager::~LootTableManager() {
}

LootTableManager& LootTableManager::GetInstance() {
    std::call_once(initFlag_, []() {
        instance_ = std::make_unique<LootTableManager>();
    });
    return *instance_;
}

void LootTableManager::RegisterTable(const LootTable& table) {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    if (lootTables_.find(table.tableId) != lootTables_.end()) {
        Logger::Warn("LootTableManager: Overwriting existing table '{}'", table.tableId);
    }
    
    lootTables_[table.tableId] = table;
    Logger::Debug("LootTableManager: Registered table '{}' with {} entries", 
                  table.tableId, table.entries.size());
}

void LootTableManager::UnregisterTable(const std::string& tableId) {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    auto it = lootTables_.find(tableId);
    if (it != lootTables_.end()) {
        lootTables_.erase(it);
        Logger::Debug("LootTableManager: Unregistered table '{}'", tableId);
    } else {
        Logger::Warn("LootTableManager: Table '{}' not found for unregister", tableId);
    }
}

const LootTable* LootTableManager::GetTable(const std::string& tableId) const {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    auto it = lootTables_.find(tableId);
    if (it != lootTables_.end()) {
        return &it->second;
    }
    
    Logger::Warn("LootTableManager: Table '{}' not found", tableId);
    return nullptr;
}

bool LootTableManager::HasTable(const std::string& tableId) const {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    return lootTables_.find(tableId) != lootTables_.end();
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> LootTableManager::GenerateLoot(
    const std::string& tableId,
    int playerLevel,
    float luckMultiplier,
    const std::unordered_map<std::string, float>& factionRep
) {
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> result;
    
    const LootTable* table = GetTable(tableId);
    if (!table) {
        Logger::Error("LootTableManager: Cannot generate loot from non-existent table '{}'", tableId);
        return result;
    }
    
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    // Track which items have been dropped if unique drops is enabled
    std::vector<size_t> droppedIndices;
    if (table->uniqueDrops) {
        droppedIndices.reserve(table->entries.size());
    }
    
    // Process guaranteed drops first
    int guaranteedCount = 0;
    for (size_t i = 0; i < table->entries.size() && guaranteedCount < table->guaranteedDrops; ++i) {
        const LootEntry& entry = table->entries[i];
        
        if (entry.dropChance >= 1.0f && 
            PlayerMeetsRequirements(entry, playerLevel, factionRep) &&
            (!table->uniqueDrops || std::find(droppedIndices.begin(), droppedIndices.end(), i) == droppedIndices.end())) {
            
            int quantity = GetRandomInt(entry.minQuantity, entry.maxQuantity);
            auto item = CreateItemFromEntry(entry, playerLevel, luckMultiplier);
            
            if (item) {
                result.emplace_back(item, quantity);
                guaranteedCount++;
                
                if (table->uniqueDrops) {
                    droppedIndices.push_back(i);
                }
            }
        }
    }
    
    // Process random drops
    int maxRandomDrops = table->maxDrops - guaranteedCount;
    if (maxRandomDrops > 0) {
        // Calculate total weight for weighted random selection
        float totalWeight = 0.0f;
        std::vector<float> weights;
        std::vector<size_t> eligibleIndices;
        
        for (size_t i = 0; i < table->entries.size(); ++i) {
            const LootEntry& entry = table->entries[i];
            
            // Skip if already dropped and unique drops is enabled
            if (table->uniqueDrops && std::find(droppedIndices.begin(), droppedIndices.end(), i) != droppedIndices.end()) {
                continue;
            }
            
            if (PlayerMeetsRequirements(entry, playerLevel, factionRep)) {
                float adjustedChance = CalculateAdjustedDropChance(
                    entry.dropChance, 
                    luckMultiplier, 
                    playerLevel, 
                    GetRandomInt(entry.minLevel, entry.maxLevel)
                );
                
                if (adjustedChance > 0.0f) {
                    weights.push_back(adjustedChance);
                    eligibleIndices.push_back(i);
                    totalWeight += adjustedChance;
                }
            }
        }
        
        // Normalize weights
        if (totalWeight > 0.0f) {
            for (float& weight : weights) {
                weight /= totalWeight;
            }
            
            // Generate random drops
            int randomDropCount = 0;
            while (randomDropCount < maxRandomDrops && !eligibleIndices.empty()) {
                // Weighted random selection
                float randomValue = GetRandomFloat();
                float cumulativeWeight = 0.0f;
                size_t selectedIndex = 0;
                
                for (size_t i = 0; i < weights.size(); ++i) {
                    cumulativeWeight += weights[i];
                    if (randomValue <= cumulativeWeight) {
                        selectedIndex = i;
                        break;
                    }
                }
                
                size_t entryIndex = eligibleIndices[selectedIndex];
                const LootEntry& entry = table->entries[entryIndex];
                
                // Generate item
                int quantity = GetRandomInt(entry.minQuantity, entry.maxQuantity);
                auto item = CreateItemFromEntry(entry, playerLevel, luckMultiplier);
                
                if (item) {
                    result.emplace_back(item, quantity);
                    randomDropCount++;
                    
                    // Remove from eligible list if unique drops
                    if (table->uniqueDrops) {
                        eligibleIndices.erase(eligibleIndices.begin() + selectedIndex);
                        weights.erase(weights.begin() + selectedIndex);
                        
                        // Re-normalize weights
                        float newTotal = 0.0f;
                        for (float w : weights) newTotal += w;
                        if (newTotal > 0.0f) {
                            for (float& w : weights) w /= newTotal;
                        }
                    }
                }
                
                // Break if no more eligible items
                if (eligibleIndices.empty()) {
                    break;
                }
            }
        }
    }
    
    Logger::Debug("LootTableManager: Generated {} items from table '{}'", result.size(), tableId);
    return result;
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> LootTableManager::GenerateLootFromMultiple(
    const std::vector<std::string>& tableIds,
    int playerLevel,
    float luckMultiplier
) {
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> result;
    
    for (const auto& tableId : tableIds) {
        auto loot = GenerateLoot(tableId, playerLevel, luckMultiplier);
        result.insert(result.end(), loot.begin(), loot.end());
    }
    
    return result;
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> LootTableManager::GenerateWeightedLoot(
    const std::vector<LootTable>& tables,
    const std::vector<float>& weights,
    int playerLevel
) {
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> result;
    
    if (tables.empty() || tables.size() != weights.size()) {
        Logger::Error("LootTableManager: Tables and weights size mismatch");
        return result;
    }
    
    // Normalize weights
    float totalWeight = 0.0f;
    for (float weight : weights) {
        totalWeight += weight;
    }
    
    if (totalWeight <= 0.0f) {
        Logger::Error("LootTableManager: Total weight must be positive");
        return result;
    }
    
    // Select a table based on weights
    float randomValue = GetRandomFloat(0.0f, totalWeight);
    float cumulativeWeight = 0.0f;
    size_t selectedIndex = 0;
    
    for (size_t i = 0; i < weights.size(); ++i) {
        cumulativeWeight += weights[i];
        if (randomValue <= cumulativeWeight) {
            selectedIndex = i;
            break;
        }
    }
    
    // Generate loot from selected table
    const LootTable& selectedTable = tables[selectedIndex];
    std::string tableId = selectedTable.tableId;
    
    // Check if table exists in manager, if not register it temporarily
    bool temporaryRegister = !HasTable(tableId);
    if (temporaryRegister) {
        RegisterTable(selectedTable);
    }
    
    result = GenerateLoot(tableId, playerLevel, 1.0f);
    
    if (temporaryRegister) {
        UnregisterTable(tableId);
    }
    
    return result;
}

int LootTableManager::GenerateGold(const std::string& tableId, float luckMultiplier) const {
    const LootTable* table = GetTable(tableId);
    if (!table) {
        return 0;
    }
    return GenerateGold(*table, luckMultiplier);
}

int LootTableManager::GenerateGold(const LootTable& table, float luckMultiplier) const {
    if (table.minGold <= 0 || table.maxGold <= 0) {
        return 0;
    }
    
    int baseGold = GetRandomInt(table.minGold, table.maxGold);
    int adjustedGold = static_cast<int>(baseGold * table.goldMultiplier * luckMultiplier);
    
    return std::max(0, adjustedGold);
}

bool LootTableManager::LoadLootTables(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Logger::Error("LootTableManager: Failed to open file '{}'", filePath);
        return false;
    }
    
    try {
        nlohmann::json jsonData;
        file >> jsonData;
        file.close();
        
        return LoadLootTablesFromJson(jsonData);
    } catch (const std::exception& e) {
        Logger::Error("LootTableManager: Failed to parse JSON from '{}': {}", filePath, e.what());
        return false;
    }
}

bool LootTableManager::SaveLootTables(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        Logger::Error("LootTableManager: Failed to create file '{}'", filePath);
        return false;
    }
    
    try {
        nlohmann::json jsonData = SerializeAllTables();
        file << jsonData.dump(4); // Pretty print with 4 spaces
        file.close();
        
        Logger::Info("LootTableManager: Saved {} tables to '{}'", GetTableCount(), filePath);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("LootTableManager: Failed to save tables to '{}': {}", filePath, e.what());
        return false;
    }
}

bool LootTableManager::LoadLootTablesFromJson(const nlohmann::json& jsonData) {
    if (!jsonData.is_array()) {
        Logger::Error("LootTableManager: Expected JSON array of loot tables");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    size_t loadedCount = 0;
    for (const auto& tableJson : jsonData) {
        try {
            LootTable table;
            table.Deserialize(tableJson);
            
            lootTables_[table.tableId] = table;
            loadedCount++;
            
            Logger::Debug("LootTableManager: Loaded table '{}' with {} entries", 
                          table.tableId, table.entries.size());
        } catch (const std::exception& e) {
            Logger::Error("LootTableManager: Failed to load table: {}", e.what());
        }
    }
    
    Logger::Info("LootTableManager: Loaded {} loot tables from JSON", loadedCount);
    return loadedCount > 0;
}

nlohmann::json LootTableManager::SerializeAllTables() const {
    nlohmann::json jsonArray = nlohmann::json::array();
    
    std::lock_guard<std::mutex> lock(tablesMutex_);
    
    for (const auto& pair : lootTables_) {
        jsonArray.push_back(pair.second.Serialize());
    }
    
    return jsonArray;
}

void LootTableManager::ClearAllTables() {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    lootTables_.clear();
    Logger::Info("LootTableManager: Cleared all loot tables");
}

size_t LootTableManager::GetTableCount() const {
    std::lock_guard<std::mutex> lock(tablesMutex_);
    return lootTables_.size();
}

bool LootTableManager::PlayerMeetsRequirements(
    const LootEntry& entry,
    int playerLevel,
    const std::unordered_map<std::string, float>& factionRep
) {
    // Check level requirement
    if (playerLevel < entry.minLevel || playerLevel > entry.maxLevel) {
        return false;
    }
    
    // Check quest requirement
    if (!entry.requiredQuest.empty()) {
        // This would need integration with quest system
        // For now, assume no quest requirement checking
        return false;
    }
    
    // Check faction requirement
    if (!entry.requiredFaction.empty()) {
        auto it = factionRep.find(entry.requiredFaction);
        if (it == factionRep.end() || it->second < entry.factionRepRequired) {
            return false;
        }
    }
    
    return true;
}

std::shared_ptr<LootItem> LootTableManager::CreateItemFromEntry(
    const LootEntry& entry,
    int playerLevel,
    float luckMultiplier
) const {
    (void)playerLevel; // suppress unused parameter warning

    // Determine item level
    int itemLevel = GetRandomInt(entry.minLevel, entry.maxLevel);

    // Generate rarity
    LootRarity rarity = GenerateRarity(entry.minRarity, entry.maxRarity, luckMultiplier);

    // Create base item
    auto item = std::make_shared<LootItem>();
    item->SetId(entry.itemId);
    item->SetName(entry.name);
    item->SetRarity(rarity);
    item->SetLevelRequirement(itemLevel);

    // Apply rarity-based modifications
    ApplyRarityStats(item, rarity);

    // Generate random stats if applicable
    if (item->GetType() == ItemType::WEAPON || item->GetType() == ItemType::ARMOR) {
        GenerateRandomStats(item, itemLevel);
    }

    // Apply enchantments for rare+ items
    if (static_cast<int>(rarity) >= static_cast<int>(LootRarity::RARE)) {
        ApplyRandomEnchantment(item, rarity);
    }

    return item;
}

LootRarity LootTableManager::GenerateRarity(
    LootRarity minRarity,
    LootRarity maxRarity,
    float luckMultiplier
) const {
    int minValue = static_cast<int>(minRarity);
    int maxValue = static_cast<int>(maxRarity);
    
    if (minValue > maxValue) {
        std::swap(minValue, maxValue);
    }
    
    // Calculate probabilities for each rarity tier
    std::vector<float> probabilities = {
        0.60f, // COMMON
        0.25f, // UNCOMMON
        0.10f, // RARE
        0.04f, // EPIC
        0.009f, // LEGENDARY
        0.001f  // MYTHIC
    };
    
    // Apply luck multiplier to improve chances
    for (int i = minValue + 1; i <= maxValue; ++i) {
        probabilities[i] *= luckMultiplier;
    }
    
    // Normalize probabilities
    float total = 0.0f;
    for (int i = minValue; i <= maxValue; ++i) {
        total += probabilities[i];
    }
    
    // Select rarity based on probabilities
    float randomValue = GetRandomFloat(0.0f, total);
    float cumulative = 0.0f;
    
    for (int i = minValue; i <= maxValue; ++i) {
        cumulative += probabilities[i];
        if (randomValue <= cumulative) {
            return static_cast<LootRarity>(i);
        }
    }
    
    return maxRarity; // Fallback
}

float LootTableManager::CalculateAdjustedDropChance(
    float baseChance,
    float luckMultiplier,
    int playerLevel,
    int itemLevel
) const {
    if (baseChance <= 0.0f) {
        return 0.0f;
    }
    
    // Adjust based on luck multiplier
    float adjustedChance = baseChance * luckMultiplier;
    
    // Adjust based on level difference (higher level items are rarer for lower level players)
    int levelDiff = itemLevel - playerLevel;
    if (levelDiff > 0) {
        adjustedChance /= (1.0f + (levelDiff * 0.1f)); // 10% reduction per level above player
    } else if (levelDiff < 0) {
        adjustedChance *= (1.0f - (abs(levelDiff) * 0.05f)); // 5% reduction per level below player
    }
    
    // Clamp between 0 and 1
    return std::clamp(adjustedChance, 0.0f, 1.0f);
}

void LootTableManager::ApplyRarityStats(std::shared_ptr<LootItem> item, LootRarity rarity) const {
    // Apply stat multipliers based on rarity
    float multiplier = 1.0f;
    
    switch (rarity) {
        case LootRarity::COMMON:
            multiplier = 1.0f;
            break;
        case LootRarity::UNCOMMON:
            multiplier = 1.2f;
            break;
        case LootRarity::RARE:
            multiplier = 1.5f;
            break;
        case LootRarity::EPIC:
            multiplier = 2.0f;
            break;
        case LootRarity::LEGENDARY:
            multiplier = 3.0f;
            break;
        case LootRarity::MYTHIC:
            multiplier = 5.0f;
            break;
    }
    
    // Apply multiplier to all stats
    auto stats = item->GetStats();
    for (const auto& stat : stats) {
        item->AddStat(stat.statName, stat.baseValue * multiplier, stat.maxValue * multiplier);
    }
    
    // Set icon color based on rarity
    glm::vec3 color;
    switch (rarity) {
        case LootRarity::COMMON: color = glm::vec3(1.0f, 1.0f, 1.0f); break; // White
        case LootRarity::UNCOMMON: color = glm::vec3(0.0f, 1.0f, 0.0f); break; // Green
        case LootRarity::RARE: color = glm::vec3(0.0f, 0.5f, 1.0f); break; // Blue
        case LootRarity::EPIC: color = glm::vec3(0.8f, 0.0f, 0.8f); break; // Purple
        case LootRarity::LEGENDARY: color = glm::vec3(1.0f, 0.5f, 0.0f); break; // Orange
        case LootRarity::MYTHIC: color = glm::vec3(1.0f, 0.0f, 0.0f); break; // Red
    }
    
    item->SetIconColor(color);
}

void LootTableManager::GenerateRandomStats(std::shared_ptr<LootItem> item, int itemLevel) const {
    // Generate 1-3 random stats based on item level
    int numStats = GetRandomInt(1, std::min(3, itemLevel / 10 + 1));
    
    // List of possible stats
    std::vector<std::pair<std::string, float>> possibleStats = {
        {"strength", 1.0f},
        {"dexterity", 1.0f},
        {"intelligence", 1.0f},
        {"vitality", 1.0f},
        {"attack_damage", 5.0f},
        {"attack_speed", 0.1f},
        {"critical_chance", 0.01f},
        {"critical_damage", 0.1f},
        {"armor", 2.0f},
        {"magic_resist", 2.0f},
        {"health", 10.0f},
        {"mana", 10.0f}
    };
    
    // Shuffle and select random stats
    std::vector<size_t> indices(possibleStats.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng_);
    
    for (int i = 0; i < numStats && i < static_cast<int>(indices.size()); ++i) {
        size_t idx = indices[i];
        const auto& stat = possibleStats[idx];
        
        // Calculate value based on item level
        float baseValue = stat.second * (1.0f + (itemLevel / 10.0f));
        float variance = GetRandomFloat(0.8f, 1.2f);
        float finalValue = baseValue * variance;
        
        item->AddStat(stat.first, finalValue, finalValue * 1.5f);
    }
}

void LootTableManager::ApplyRandomEnchantment(std::shared_ptr<LootItem> item, LootRarity rarity) const {
    // List of possible enchantments
    std::vector<ItemModifier> possibleEnchantments = {
        {"multiply", "attack_damage", 1.25f, 0, "enchantment"},
        {"multiply", "attack_speed", 1.15f, 0, "enchantment"},
        {"add", "critical_chance", 0.05f, 0, "enchantment"},
        {"multiply", "critical_damage", 1.3f, 0, "enchantment"},
        {"multiply", "armor", 1.2f, 0, "enchantment"},
        {"multiply", "magic_resist", 1.2f, 0, "enchantment"},
        {"add", "health", 50.0f, 0, "enchantment"},
        {"add", "mana", 30.0f, 0, "enchantment"}
    };
    
    // Number of enchantments based on rarity
    int numEnchantments = static_cast<int>(rarity) - static_cast<int>(LootRarity::RARE) + 1;
    numEnchantments = std::min(numEnchantments, 3);
    
    // Shuffle and select random enchantments
    std::vector<size_t> indices(possibleEnchantments.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng_);
    
    for (int i = 0; i < numEnchantments; ++i) {
        if (i < static_cast<int>(indices.size())) {
            item->AddModifier(possibleEnchantments[indices[i]]);
        }
    }
}

float LootTableManager::GetRandomFloat(float min, float max) const {
    std::lock_guard<std::mutex> lock(rngMutex_);
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

int LootTableManager::GetRandomInt(int min, int max) const {
    std::lock_guard<std::mutex> lock(rngMutex_);
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng_);
}
