#include "game/NPCSystem.hpp"

// =============== NPCManager Implementation ===============

NPCManager::NPCManager() : nextNPCId_(1000), nextSquadId_(1) {
}

uint64_t NPCManager::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto npc = std::make_unique<NPCEntity>(type, position, ownerId);
    uint64_t npcId = nextNPCId_++;
    npc->SetId(npcId);

    NPCEntity* npcPtr = npc.get();
    npcs_[npcId] = std::move(npc);

    return npcId;
}

void NPCManager::DespawnNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    npcs_.erase(npcId);
}

NPCEntity* NPCManager::GetNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = npcs_.find(npcId);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

void NPCManager::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [npcId, npc] : npcs_) {
        if (!npc) continue;
        UpdateNPCBehavior(npcId, deltaTime);
    }
}

void NPCManager::UpdateNPCBehavior(uint64_t npcId, float deltaTime) {
    auto it = npcs_.find(npcId);
    if (it == npcs_.end()) {
        return;
    }

    NPCEntity* npc = it->second.get();
    if (!npc) {
        return;
    }

    ProcessNPCAI(npc, deltaTime);
}

void NPCManager::FormSquad(const std::vector<uint64_t>& npcIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t squadId = nextSquadId_++;
    squads_[squadId] = npcIds;
}

void NPCManager::BreakSquad(uint64_t squadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    squads_.erase(squadId);
}

std::vector<uint64_t> NPCManager::GetNPCsInRadius(const glm::vec3& position, float radius) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> npcs;

    for (const auto& [npcId, npc] : npcs_) {
        if (!npc) continue;
        float distance = glm::distance(position, npc->GetPosition());
        if (distance <= radius) {
            npcs.push_back(npcId);
        }
    }

    return npcs;
}

void NPCManager::ProcessNPCAI(NPCEntity* npc, float deltaTime) {
    npc->Update(deltaTime);
    HandleCombat(npc, deltaTime);
    HandleMovement(npc, deltaTime);
}

void NPCManager::HandleCombat(NPCEntity* npc, float deltaTime) {
    if (npc->GetAIState() == NPCAIState::COMBAT) {
        // Check if target is in attack range
        // If yes, attack
        // If no, chase
        npc->PerformAttack();
    }
}

void NPCManager::HandleMovement(NPCEntity* npc, float deltaTime) {
    // Update position based on velocity
    glm::vec3 newPos = npc->GetPosition() + npc->GetVelocity() * deltaTime;
    npc->SetPosition(newPos);
}

