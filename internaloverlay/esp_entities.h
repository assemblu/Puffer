#pragma once
#include <windows.h>
#include <vector>
#include "esp_config.h"
#include "esp_globals.h"

struct EspPlayer {
    int slot;
    uint32_t health;
    bool alive;
    uint8_t team;
    float position[3];
    uintptr_t entity;
    uintptr_t controller;
    bool isLocal;
};

void UpdateEntities(const EspConfig& config, const EspGlobals& globals, std::vector<EspPlayer>& out);
