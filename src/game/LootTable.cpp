#include <fstream>
#include <algorithm>

#include "game/LootTable.hpp"
#include "logging/Logger.hpp"

nlohmann::json LootEntry::Serialize() const {
    return {
        {"itemId", itemId},
        {"dropChance", dropChance},
        {"minQuantity", minQuantity},
        {"maxQuantity", maxQuantity},
        {"minLevel", minLevel},
        {"maxLevel", maxLevel},
        {"minRarity", static_cast<int>(minRarity)},
        {"maxRarity", static_cast<int>(maxRarity)},
        {"requiredQuest", requiredQuest},
        {"requiredFaction", requiredFaction},
        {"factionRepRequired", factionRepRequired}
    };
}

void LootEntry::Deserialize(const nlohmann::json& data) {
    itemId = data["itemId"];
    dropChance = data["dropChance"];
    minQuantity = data["minQuantity"];
    maxQuantity = data["maxQuantity"];
    minLevel = data["minLevel"];
    maxLevel = data["maxLevel"];
    minRarity = static_cast<LootRarity>(data["minRarity"]);
    maxRarity = static_cast<LootRarity>(data["maxRarity"]);
    requiredQuest = data["requiredQuest"];
    requiredFaction = data["requiredFaction"];
    factionRepRequired = data["factionRepRequired"];
}

nlohmann::json LootTable::Serialize() const {
    nlohmann::json entriesArray = nlohmann::json::array();
    for (const auto& entry : entries) {
        entriesArray.push_back(entry.Serialize());
    }
    
    return {
        {"tableId", tableId},
        {"name", name},
        {"entries", entriesArray},
        {"guaranteedDrops", guaranteedDrops},
        {"maxDrops", maxDrops},
        {"uniqueDrops", uniqueDrops},
        {"goldMultiplier", goldMultiplier},
        {"minGold", minGold},
        {"maxGold", maxGold}
    };
}

void LootTable::Deserialize(const nlohmann::json& data) {
    tableId = data["tableId"];
    name = data["name"];
    
    entries.clear();
    for (const auto& entryData : data["entries"]) {
        LootEntry entry;
        entry.Deserialize(entryData);
        entries.push_back(entry);
    }
    
    guaranteedDrops = data["guaranteedDrops"];
    maxDrops = data["maxDrops"];
    uniqueDrops = data["uniqueDrops"];
    goldMultiplier = data["goldMultiplier"];
    minGold = data["minGold"];
    maxGold = data["maxGold"];
}
