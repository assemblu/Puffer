#include "esp_globals.h"
#include "logger.h"

static bool IsReadable(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi = {};
    return VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != 0 && mbi.State == MEM_COMMIT;
}

bool InitGlobals(const EspConfig& config, EspGlobals& out) {
    HMODULE hClient = GetModuleHandleW(L"client.dll");
    if (!hClient) {
        OverlayLog(L"[globals] client.dll not loaded");
        out.valid = false;
        return false;
    }

    out.clientBase = (uintptr_t)hClient;
    out.controllerGlobalAddr = out.clientBase + config.dwLocalPlayerController;
    out.viewMatrixGlobalAddr = out.clientBase + config.dwViewMatrix;
    out.entityListGlobalAddr = out.clientBase + config.dwEntityList;

    RefreshGlobals(out);

    if (out.valid) {
        OverlayLog(L"[globals] Init OK — Ctrl: 0x%p, VM: 0x%p, EL: 0x%p",
                   out.controllerArray, out.viewMatrix, out.entityListChunks);
    } else {
        OverlayLog(L"[globals] Init FAILED — one or more globals invalid");
    }
    return out.valid;
}

void RefreshGlobals(EspGlobals& g) {
    g.valid = false;

    // Controller array: global holds pointer to 64-slot array
    g.controllerArray = nullptr;
    if (IsReadable(g.controllerGlobalAddr)) {
        uintptr_t val = *(uintptr_t*)g.controllerGlobalAddr;
        if (val && IsReadable(val)) {
            g.controllerArray = (uintptr_t*)val;
        }
    }

    // View matrix: lea rcx,unk_2331B30 means global IS the data (not a pointer)
    // Function computes: rcx + (slot*64) for per-player matrices
    // Slot 0 = local player camera
    g.viewMatrix = nullptr;
    if (IsReadable(g.viewMatrixGlobalAddr)) {
        g.viewMatrix = (const float*)g.viewMatrixGlobalAddr;
    }

    // Entity list: global holds pointer to chunk array base
    g.entityListChunks = nullptr;
    if (IsReadable(g.entityListGlobalAddr)) {
        uintptr_t val = *(uintptr_t*)g.entityListGlobalAddr;
        if (val && IsReadable(val)) {
            g.entityListChunks = (uintptr_t*)val;
        }
    }

    g.valid = g.controllerArray != nullptr &&
              g.viewMatrix != nullptr &&
              g.entityListChunks != nullptr;
}
