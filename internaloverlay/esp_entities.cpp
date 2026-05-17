#include "esp_entities.h"
#include "logger.h"

static bool IsReadable(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi = {};
    return VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != 0 && mbi.State == MEM_COMMIT;
}

static bool ResolveEntity(const EspConfig& config, const EspGlobals& globals,
                          uint32_t handle, uintptr_t& outEntity) {
    uint32_t index = handle & 0x7FFF;
    if (index == 0 || index == 0x7FFF) return false;

    int chunk = index >> 9;
    int entry = index & 0x1FF;

    if (chunk >= (int)config.chunkCount) return false;

    uintptr_t chunkBase = globals.entityListChunks[chunk];
    if (!chunkBase || !IsReadable(chunkBase)) return false;

    uintptr_t slot = chunkBase + (entry * config.entryStride);
    if (!IsReadable(slot + config.entityPtrOffset)) return false;

    outEntity = *(uintptr_t*)(slot + config.entityPtrOffset);
    return outEntity != 0 && IsReadable(outEntity);
}

static bool IsValidPosition(const float pos[3]) {
    for (int i = 0; i < 3; i++) {
        if (pos[i] != pos[i]) return false;
        if (pos[i] < -100000.f || pos[i] > 100000.f) return false;
    }
    return true;
}

void UpdateEntities(const EspConfig& config, const EspGlobals& globals, std::vector<EspPlayer>& out) {
    out.clear();

    if (!globals.valid || !globals.controllerArray || !globals.entityListChunks) {
        return;
    }

    if (!IsReadable((uintptr_t)globals.controllerArray)) return;
    if (!IsReadable((uintptr_t)globals.entityListChunks)) return;

    int aliveCount = 0;
    EspPlayer* localPlayer = nullptr;

    for (int slot = 0; slot < 64; slot++) {
        uintptr_t ctrl = globals.controllerArray[slot];
        if (!ctrl || !IsReadable(ctrl)) continue;

        uint32_t health = 0;
        bool alive = false;
        if (IsReadable(ctrl + config.m_iPawnHealth)) {
            health = *(uint32_t*)(ctrl + config.m_iPawnHealth);
        }
        if (IsReadable(ctrl + config.m_bPawnIsAlive)) {
            alive = (*(uint8_t*)(ctrl + config.m_bPawnIsAlive)) != 0;
        }

        uint32_t handle = 0;
        if (!IsReadable(ctrl + config.m_hPlayerPawn)) continue;
        handle = *(uint32_t*)(ctrl + config.m_hPlayerPawn);
        if (!handle || (handle & 0x7FFF) == 0) continue;

        uintptr_t entity = 0;
        if (!ResolveEntity(config, globals, handle, entity)) continue;

        uintptr_t sceneNode = 0;
        if (!IsReadable(entity + config.m_pGameSceneNode)) continue;
        sceneNode = *(uintptr_t*)(entity + config.m_pGameSceneNode);
        if (!sceneNode || !IsReadable(sceneNode)) continue;

        float pos[3] = { 0 };
        if (!IsReadable((uintptr_t)(sceneNode + config.m_vecAbsOrigin))) continue;
        memcpy(pos, (void*)(sceneNode + config.m_vecAbsOrigin), sizeof(pos));
        if (!IsValidPosition(pos)) continue;

        uint8_t team = 0;
        if (config.m_iTeamNum && IsReadable(entity + config.m_iTeamNum)) {
            team = *(uint8_t*)(entity + config.m_iTeamNum);
        }

        EspPlayer p;
        p.slot = slot;
        p.health = health;
        p.alive = alive;
        p.team = team;
        memcpy(p.position, pos, sizeof(p.position));
        p.entity = entity;
        p.controller = ctrl;
        p.isLocal = false;
        out.push_back(p);

        if (alive) {
            aliveCount++;
            if (!localPlayer) localPlayer = &p;
        }
    }

    if (localPlayer) localPlayer->isLocal = true;

    if (aliveCount > 0) {
        OverlayLog(L"[entities] %d total, %d alive", (int)out.size(), aliveCount);
    }
}
