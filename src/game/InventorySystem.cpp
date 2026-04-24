#include "game/InventorySystem.hpp"

nlohmann::json InventorySlot::Serialize() const {
    nlohmann::json result = {
        {"quantity", quantity},
        {"equipped", equipped},
        {"position", position}
    };
    if (item) {
        result["item"] = item->Serialize();
    }
    return result;
}

void InventorySlot::Deserialize(const nlohmann::json& data) {
    quantity = data["quantity"];
    equipped = data["equipped"];
    position = data["position"];
    if (data.contains("item") && !data["item"].is_null()) {
        item = std::make_shared<LootItem>();
        item->Deserialize(data["item"]);
    }
}

InventorySystem& InventorySystem::GetInstance() {
    static InventorySystem instance;
    return instance;
}

bool InventorySystem::AddItem(uint64_t playerId, const LootItem& item, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quantity <= 0) return false;
    auto& inventory = playerInventories_[playerId];
    if (item.GetMaxStackSize() > 1) {
        for (auto& slot : inventory.inventorySlots) {
            if (slot.item && slot.item->CanStackWith(item) &&
                slot.quantity < slot.item->GetMaxStackSize()) {
                int canAdd = slot.item->GetMaxStackSize() - slot.quantity;
                int toAdd = std::min(quantity, canAdd);
                slot.quantity += toAdd;
                quantity -= toAdd;
                if (quantity == 0) {
                    SaveInventory(playerId);
                    return true;
                }
            }
        }
    }
    while (quantity > 0) {
        if (inventory.inventorySlots.size() >= (uint64_t)inventory.maxInventorySize) {
            Logger::Error("Inventory full for player {}", playerId);
            SaveInventory(playerId);
            return false;
        }
        int stackSize = std::min(quantity, item.GetMaxStackSize());
        InventorySlot newSlot;
        newSlot.item = std::make_shared<LootItem>(item);
        newSlot.item->SetStackSize(stackSize);
        newSlot.quantity = stackSize;
        newSlot.position = inventory.inventorySlots.size();
        inventory.inventorySlots.push_back(newSlot);
        quantity -= stackSize;
    }
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::RemoveItem(uint64_t playerId, uint64_t itemId, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quantity <= 0) return false;
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    int remaining = quantity;
    for (auto& slot : inventory.inventorySlots) {
        if (slot.item && slot.item->GetId() == itemId) {
            int toRemove = std::min(remaining, slot.quantity);
            slot.quantity -= toRemove;
            remaining -= toRemove;
            if (slot.quantity == 0) {
                slot.item.reset();
            }
            if (remaining == 0) {
                inventory.inventorySlots.erase(
                    std::remove_if(inventory.inventorySlots.begin(),
                                  inventory.inventorySlots.end(),
                                  [](const InventorySlot& slot) {
                                      return slot.quantity == 0;
                                  }),
                    inventory.inventorySlots.end()
                );
                for (size_t i = 0; i < inventory.inventorySlots.size(); ++i) {
                    inventory.inventorySlots[i].position = i;
                }
                SaveInventory(playerId);
                return true;
            }
        }
    }
    SaveInventory(playerId);
    return false;
}

bool InventorySystem::UseItem(uint64_t playerId, uint64_t itemId, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    for (auto& slot : inventory.inventorySlots) {
        if (slot.item && slot.item->GetId() == itemId && slot.quantity >= quantity) {
            if (!slot.item->IsConsumable()) return false;
            slot.quantity -= quantity;
            if (slot.quantity == 0) {
                slot.item.reset();
            }
            SaveInventory(playerId);
            return true;
        }
    }
    return false;
}

bool InventorySystem::MoveItem(uint64_t playerId, int fromSlot, int toSlot) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end())
        return false;
    auto& inventory = it->second;
    if (fromSlot < 0 || fromSlot >= static_cast<int>(inventory.inventorySlots.size()) ||
        toSlot < 0 || toSlot >= static_cast<int>(inventory.inventorySlots.size()))
        return false;
    if (fromSlot == toSlot)
        return true;
    auto& slotFrom = inventory.inventorySlots[fromSlot];
    auto& slotTo = inventory.inventorySlots[toSlot];
    std::swap(slotFrom.item, slotTo.item);
    std::swap(slotFrom.quantity, slotTo.quantity);
    std::swap(slotFrom.equipped, slotTo.equipped);
    slotFrom.position = fromSlot;
    slotTo.position = toSlot;
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::SwapItems(uint64_t playerId, int slot1, int slot2) {
    return MoveItem(playerId, slot1, slot2);
}

bool InventorySystem::SplitStack(uint64_t playerId, int slot, int splitQuantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    if (slot < 0 || slot >= static_cast<int>(inventory.inventorySlots.size())) return false;
    auto& sourceSlot = inventory.inventorySlots[slot];
    if (!sourceSlot.item || sourceSlot.quantity <= splitQuantity) return false;
    if (inventory.inventorySlots.size() >= (uint64_t)inventory.maxInventorySize) return false;
    InventorySlot newSlot;
    newSlot.item = std::make_shared<LootItem>(*sourceSlot.item);
    newSlot.item->SetStackSize(splitQuantity);
    newSlot.quantity = splitQuantity;
    newSlot.position = inventory.inventorySlots.size();
    sourceSlot.quantity -= splitQuantity;
    sourceSlot.item->SetStackSize(sourceSlot.quantity);
    inventory.inventorySlots.push_back(newSlot);
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::MergeStacks(uint64_t playerId, int sourceSlot, int targetSlot) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    if (sourceSlot < 0 || sourceSlot >= static_cast<int>(inventory.inventorySlots.size()) ||
        targetSlot < 0 || targetSlot >= static_cast<int>(inventory.inventorySlots.size()))
        return false;
    if (sourceSlot == targetSlot) return true;
    auto& source = inventory.inventorySlots[sourceSlot];
    auto& target = inventory.inventorySlots[targetSlot];
    if (!source.item || !target.item) return false;
    if (!source.item->CanStackWith(*target.item)) return false;
    int maxStack = source.item->GetMaxStackSize();
    int space = maxStack - target.quantity;
    if (space <= 0) return false;
    int move = std::min(source.quantity, space);
    target.quantity += move;
    source.quantity -= move;
    if (source.quantity == 0) {
        source.item.reset();
        inventory.inventorySlots.erase(inventory.inventorySlots.begin() + sourceSlot);
        for (size_t i = 0; i < inventory.inventorySlots.size(); ++i) {
            inventory.inventorySlots[i].position = i;
        }
    }
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::EquipItem(uint64_t playerId, int inventorySlot, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    if (inventorySlot < 0 || inventorySlot >= static_cast<int>(inventory.inventorySlots.size()))
        return false;
    auto& slot = inventory.inventorySlots[inventorySlot];
    if (!slot.item || !slot.item->IsEquippable() || slot.quantity < quantity) return false;
    if (!MeetsRequirements(playerId, *slot.item)) return false;
    int equipSlot = GetEquipmentSlotForItem(*slot.item);
    if (equipSlot == -1 || equipSlot >= inventory.maxEquipmentSlots) return false;
    if ((uint64_t)equipSlot >= inventory.equipmentSlots.size())
        inventory.equipmentSlots.resize(inventory.maxEquipmentSlots);
    auto& equipped = inventory.equipmentSlots[equipSlot];
    if (equipped.quantity > 0) {
        std::swap(equipped, slot);
        slot.position = inventorySlot;
        equipped.equipped = true;
    } else {
        equipped = slot;
        slot.item.reset();
        slot.quantity = 0;
        inventory.inventorySlots.erase(inventory.inventorySlots.begin() + inventorySlot);
        for (size_t i = 0; i < inventory.inventorySlots.size(); ++i)
            inventory.inventorySlots[i].position = i;
    }
    equipped.equipped = true;
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::UnequipItem(uint64_t playerId, int equipmentSlot, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    auto& inventory = it->second;
    if (equipmentSlot < 0 || equipmentSlot >= static_cast<int>(inventory.equipmentSlots.size()))
        return false;
    auto& slot = inventory.equipmentSlots[equipmentSlot];
    if (!slot.item || slot.quantity < quantity) return false;
    if (inventory.inventorySlots.size() >= (uint64_t)inventory.maxInventorySize) return false;
    InventorySlot newSlot;
    newSlot.item = std::make_shared<LootItem>(*slot.item);
    newSlot.quantity = quantity;
    newSlot.position = inventory.inventorySlots.size();
    slot.quantity -= quantity;
    if (slot.quantity == 0) {
        slot.item.reset();
        slot.equipped = false;
    }
    inventory.inventorySlots.push_back(newSlot);
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::AutoEquip(uint64_t playerId, uint64_t itemId, int quantity) {
    int slot = FindItemSlot(playerId, itemId);
    if (slot == -1) return false;
    return EquipItem(playerId, slot, quantity);
}

std::shared_ptr<LootItem> InventorySystem::GetItem(uint64_t playerId, int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return nullptr;
    if (slot < 0 || slot >= static_cast<int>(it->second.inventorySlots.size())) return nullptr;
    return it->second.inventorySlots[slot].item;
}

int InventorySystem::GetItemCount(uint64_t playerId, uint64_t itemId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return 0;
    int count = 0;
    for (const auto& slot : it->second.inventorySlots) {
        if (slot.item && slot.item->GetId() == itemId) count += slot.quantity;
    }
    return count;
}

bool InventorySystem::HasItem(uint64_t playerId, uint64_t itemId, int quantity) const {
    return GetItemCount(playerId, itemId) >= quantity;
}

std::vector<InventorySlot> InventorySystem::GetInventory(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return {};
    return it->second.inventorySlots;
}

std::vector<InventorySlot> InventorySystem::GetEquipment(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return {};
    return it->second.equipmentSlots;
}

int InventorySystem::GetFreeSlots(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return 0;
    return it->second.maxInventorySize - it->second.inventorySlots.size();
}

int InventorySystem::GetTotalSlots(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return 0;
    return it->second.maxInventorySize;
}

bool InventorySystem::HasSpaceFor(uint64_t playerId, const LootItem& item, int quantity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    const auto& inventory = it->second;
    int remaining = quantity;
    if (item.GetMaxStackSize() > 1) {
        for (const auto& slot : inventory.inventorySlots) {
            if (slot.item && slot.item->CanStackWith(item)) {
                int space = slot.item->GetMaxStackSize() - slot.quantity;
                if (space > 0) {
                    remaining -= space;
                    if (remaining <= 0) return true;
                }
            }
        }
    }
    int neededSlots = (remaining + item.GetMaxStackSize() - 1) / item.GetMaxStackSize();
    return (inventory.inventorySlots.size() + neededSlots) <= (uint64_t)inventory.maxInventorySize;
}

bool InventorySystem::CanTradeItem(uint64_t playerId, uint64_t itemId) const {
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    for (const auto& slot : it->second.inventorySlots) {
        if (slot.item && slot.item->GetId() == itemId) {
            return true; // Trading allowed by default, could add tradable flag
        }
    }
    return false;
}

bool InventorySystem::TransferItem(uint64_t fromPlayerId, uint64_t toPlayerId, uint64_t itemId, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto fromIt = playerInventories_.find(fromPlayerId);
    auto toIt = playerInventories_.find(toPlayerId);
    if (fromIt == playerInventories_.end() || toIt == playerInventories_.end()) return false;
    if (!HasItem(fromPlayerId, itemId, quantity)) return false;
    auto& fromInv = fromIt->second;
    auto& toInv = toIt->second;
    LootItem* sourceItem = nullptr;
    int sourceSlot = -1;
    for (size_t i = 0; i < fromInv.inventorySlots.size(); ++i) {
        if (fromInv.inventorySlots[i].item && fromInv.inventorySlots[i].item->GetId() == itemId) {
            sourceItem = fromInv.inventorySlots[i].item.get();
            sourceSlot = i;
            break;
        }
    }
    if (!sourceItem) return false;
    int remaining = quantity;
    if (sourceItem->GetMaxStackSize() > 1) {
        for (auto& slot : toInv.inventorySlots) {
            if (slot.item && slot.item->CanStackWith(*sourceItem)) {
                int space = slot.item->GetMaxStackSize() - slot.quantity;
                int add = std::min(remaining, space);
                slot.quantity += add;
                remaining -= add;
                if (remaining == 0) break;
            }
        }
    }
    while (remaining > 0) {
        if (toInv.inventorySlots.size() >= (uint64_t)toInv.maxInventorySize) return false;
        int stackSize = std::min(remaining, sourceItem->GetMaxStackSize());
        InventorySlot newSlot;
        newSlot.item = std::make_shared<LootItem>(*sourceItem);
        newSlot.item->SetStackSize(stackSize);
        newSlot.quantity = stackSize;
        newSlot.position = toInv.inventorySlots.size();
        toInv.inventorySlots.push_back(newSlot);
        remaining -= stackSize;
    }
    fromInv.inventorySlots[sourceSlot].quantity -= quantity;
    if (fromInv.inventorySlots[sourceSlot].quantity == 0) {
        fromInv.inventorySlots.erase(fromInv.inventorySlots.begin() + sourceSlot);
        for (size_t i = 0; i < fromInv.inventorySlots.size(); ++i)
            fromInv.inventorySlots[i].position = i;
    }
    SaveInventory(fromPlayerId);
    SaveInventory(toPlayerId);
    return true;
}

bool InventorySystem::LoadInventory(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return LoadFromDatabase(playerId);
}

bool InventorySystem::SaveInventory(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return SaveToDatabase(playerId);
}

nlohmann::json InventorySystem::SerializeInventory(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return {};
    const auto& inventory = it->second;
    nlohmann::json result;
    nlohmann::json invArray = nlohmann::json::array();
    for (const auto& slot : inventory.inventorySlots) {
        invArray.push_back(slot.Serialize());
    }
    result["inventory"] = invArray;
    nlohmann::json equipArray = nlohmann::json::array();
    for (const auto& slot : inventory.equipmentSlots) {
        equipArray.push_back(slot.Serialize());
    }
    result["equipment"] = equipArray;
    result["gold"] = inventory.gold;
    result["maxInventorySize"] = inventory.maxInventorySize;
    result["maxEquipmentSlots"] = inventory.maxEquipmentSlots;
    return result;
}

bool InventorySystem::DeserializeInventory(uint64_t playerId, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayerInventory inventory;
    try {
        if (data.contains("inventory")) {
            for (const auto& slotData : data["inventory"]) {
                InventorySlot slot;
                slot.Deserialize(slotData);
                inventory.inventorySlots.push_back(slot);
            }
        }
        if (data.contains("equipment")) {
            for (const auto& slotData : data["equipment"]) {
                InventorySlot slot;
                slot.Deserialize(slotData);
                inventory.equipmentSlots.push_back(slot);
            }
        }
        inventory.gold = data.value("gold", 0);
        inventory.maxInventorySize = data.value("maxInventorySize", 40);
        inventory.maxEquipmentSlots = data.value("maxEquipmentSlots", 12);
        inventory.equipmentSlots.resize(inventory.maxEquipmentSlots);
        playerInventories_[playerId] = inventory;
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to deserialize inventory for player {}: {}", playerId, e.what());
        return false;
    }
}

int64_t InventorySystem::GetGold(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return 0;
    return it->second.gold;
}

bool InventorySystem::AddGold(uint64_t playerId, int64_t amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) {
        playerInventories_[playerId] = PlayerInventory();
        it = playerInventories_.find(playerId);
    }
    if (amount < 0) return false;
    it->second.gold += amount;
    if (it->second.gold < 0) it->second.gold = INT64_MAX;
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::RemoveGold(uint64_t playerId, int64_t amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    if (amount < 0) return false;
    if (it->second.gold < amount) return false;
    it->second.gold -= amount;
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::TransferGold(uint64_t fromPlayerId, uint64_t toPlayerId, int64_t amount) {
    if (fromPlayerId == toPlayerId || amount <= 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto fromIt = playerInventories_.find(fromPlayerId);
    auto toIt = playerInventories_.find(toPlayerId);
    if (fromIt == playerInventories_.end() || toIt == playerInventories_.end()) return false;
    if (fromIt->second.gold < amount) return false;
    fromIt->second.gold -= amount;
    toIt->second.gold += amount;
    if (toIt->second.gold < 0) toIt->second.gold = INT64_MAX;
    SaveInventory(fromPlayerId);
    SaveInventory(toPlayerId);
    return true;
}

bool InventorySystem::ValidateSlot(uint64_t playerId, int slot) const {
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    return slot >= 0 && slot < it->second.maxInventorySize;
}

int InventorySystem::FindItemSlot(uint64_t playerId, uint64_t itemId) const {
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return -1;
    for (size_t i = 0; i < it->second.inventorySlots.size(); ++i) {
        if (it->second.inventorySlots[i].item && it->second.inventorySlots[i].item->GetId() == itemId)
            return i;
    }
    return -1;
}

int InventorySystem::FindFreeSlot(uint64_t playerId) const {
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return -1;
    for (size_t i = 0; i < it->second.inventorySlots.size(); ++i) {
        if (!it->second.inventorySlots[i].item) return i;
    }
    return it->second.inventorySlots.size();
}

bool InventorySystem::CanStackWithSlot(const InventorySlot& slot, const LootItem& item) const {
    return slot.item && slot.item->CanStackWith(item);
}

bool InventorySystem::IsEquipmentSlot(int slot) const {
    return slot >= PlayerInventory::HEAD && slot <= PlayerInventory::TRINKET2;
}

int InventorySystem::GetEquipmentSlotForItem(const LootItem& item) const {
    switch (item.GetType()) {
        case LootType::WEAPON: return PlayerInventory::MAIN_HAND;
        case LootType::ARMOR: return PlayerInventory::CHEST;
        case LootType::JEWELRY: return PlayerInventory::RING1;
        default: return -1;
    }
}

bool InventorySystem::MeetsRequirements(uint64_t playerId, const LootItem& item) const {
    (void)playerId;
    (void)item;
    return true;
}

bool InventorySystem::LoadFromDatabase(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto& dbManager = DbManager::GetInstance();
        auto* backend = dbManager.GetBackend();
        if (!backend) {
            Logger::Error("No database backend available for player {}", playerId);
            return false;
        }
        int shardId = dbManager.GetShardId(playerId);
        std::string query = "SELECT inventory_data FROM player_inventory WHERE player_id = " + std::to_string(playerId);
        nlohmann::json result = backend->QueryShard(shardId, query);
        if (!result.empty() && result[0].contains("inventory_data")) {
            std::string inventoryJson = result[0]["inventory_data"];
            nlohmann::json data = nlohmann::json::parse(inventoryJson);
            return DeserializeInventory(playerId, data);
        }
        playerInventories_[playerId] = PlayerInventory();
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to load inventory for player {}: {}", playerId, e.what());
        return false;
    }
}

bool InventorySystem::SaveToDatabase(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto it = playerInventories_.find(playerId);
        if (it == playerInventories_.end()) return false;
        auto& dbManager = DbManager::GetInstance();
        auto* backend = dbManager.GetBackend();
        if (!backend) {
            Logger::Error("No database backend available for player {}", playerId);
            return false;
        }
        nlohmann::json data = SerializeInventory(playerId);
        std::string inventoryJson = data.dump();
        std::string escaped = backend->EscapeString(inventoryJson);
        int shardId = dbManager.GetShardId(playerId);
        std::string query = "INSERT INTO player_inventory (player_id, inventory_data) VALUES (" +
                            std::to_string(playerId) + ", '" + escaped + "') ON CONFLICT (player_id) DO UPDATE SET " +
                            "inventory_data = EXCLUDED.inventory_data, last_updated = NOW()";
        return backend->ExecuteShard(shardId, query);
    } catch (const std::exception& e) {
        Logger::Error("Failed to save inventory for player {}: {}", playerId, e.what());
        return false;
    }
}