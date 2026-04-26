#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "game/GameData.hpp"

struct LootStat {
    std::string statName;
    float baseValue;
    float currentValue;
    float maxValue;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct LootModifier {
    std::string modifierType;  // "add", "multiply", "set"
    std::string targetStat;
    float value;
    int duration = 0;  // 0 = permanent
    std::string source;  // "enchantment", "socket", "temporary"
};

class LootItem {
public:
    LootItem();
    LootItem(uint64_t id, const std::string& name = "", LootType type = LootType::MATERIAL, LootRarity rarity = LootRarity::COMMON);

    // Getters
    uint64_t GetId() const { return id_; }
    const std::string& GetName() const { return name_; }
    LootType GetType() const { return type_; }
    LootRarity GetRarity() const { return rarity_; }
    int GetStackSize() const { return stackSize_; }
    int GetMaxStackSize() const { return maxStackSize_; }
    int GetLevelRequirement() const { return levelRequirement_; }
    const glm::vec3& GetIconColor() const { return iconColor_; }

    // Setters
    void SetId(uint64_t id) { id_ = id; }
    void SetName(const std::string& name) { name_ = name; }
    void SetRarity(LootRarity rarity = LootRarity::COMMON) { rarity_ = rarity; }
    void SetStackSize(int size);
    void SetLevelRequirement(int level);
    void SetIconColor(const glm::vec3& color);

    // Stats management
    void AddStat(const std::string& name, float baseValue, float maxValue = 0.0f);
    LootStat* GetStat(const std::string& name);
    const std::vector<LootStat>& GetStats() const { return stats_; }

    // Modifiers management
    void AddModifier(const LootModifier& modifier);
    std::vector<LootModifier> GetModifiersForStat(const std::string& statName) const;

    // Serialization
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);

    // Utility
    float GetStatValue(const std::string& statName) const;
    bool CanStackWith(const LootItem& other) const;
    bool IsEquippable() const;
    bool IsConsumable() const;

private:
    uint64_t id_;
    std::string name_;
    std::string description_;
    LootType type_;
    LootRarity rarity_;

    int stackSize_ = 1;
    int maxStackSize_ = 1;
    int levelRequirement_ = 1;

    glm::vec3 iconColor_ = glm::vec3(1.0f);
    std::string iconTexture_;

    std::vector<LootStat> stats_;
    std::vector<LootModifier> modifiers_;

    // Trading properties
    bool tradable_ = true;
    bool droppable_ = true;
    bool sellable_ = true;
    int baseGoldValue_ = 0;

    // Durability (for equipment)
    float durability_ = 100.0f;
    float maxDurability_ = 100.0f;

    // Socket system (for gems/enchantments)
    int socketCount_ = 0;
    std::vector<std::string> socketedItems_;
};
