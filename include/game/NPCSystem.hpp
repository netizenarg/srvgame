#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include "game/NPCEntity.hpp"
#include "logging/Logger.hpp"

class NPCManager {
public:
    NPCManager();

    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    NPCEntity* GetNPC(uint64_t npcId);

    void Update(float deltaTime);
    void UpdateNPCBehavior(uint64_t npcId, float deltaTime);

    // Group behaviors
    void FormSquad(const std::vector<uint64_t>& npcIds);
    void BreakSquad(uint64_t squadId);

    // Area management
    std::vector<uint64_t> GetNPCsInRadius(const glm::vec3& position, float radius);

private:
    std::unordered_map<uint64_t, std::unique_ptr<NPCEntity>> npcs_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> squads_; // squadId -> npcIds

    uint64_t nextNPCId_ = 1000;
    uint64_t nextSquadId_ = 1;
    std::mutex mutex_;

    void ProcessNPCAI(NPCEntity* npc, float deltaTime);
    void HandleCombat(NPCEntity* npc, float deltaTime);
    void HandleMovement(NPCEntity* npc, float deltaTime);
};
