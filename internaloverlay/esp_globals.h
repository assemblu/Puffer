#pragma once
#include <windows.h>
#include "esp_config.h"

struct EspGlobals {
    uintptr_t clientBase;

    // Global addresses (base + rva, session-stable)
    uintptr_t controllerGlobalAddr;
    uintptr_t viewMatrixGlobalAddr;
    uintptr_t entityListGlobalAddr;

    // Dereferenced pointers (change on map load)
    uintptr_t* controllerArray;
    const float* viewMatrix;
    uintptr_t* entityListChunks;

    bool valid;
};

bool InitGlobals(const EspConfig& config, EspGlobals& out);
void RefreshGlobals(EspGlobals& g);
