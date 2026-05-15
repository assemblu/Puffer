#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <wchar.h>
#include <string.h>
#pragma comment(lib, "psapi.lib")

static const uintptr_t OFF_PAWN_IS_ALIVE = 0x914;
static const uintptr_t OFF_PAWN_HEALTH   = 0x918;
static const uintptr_t OFF_PAWN_ARMOR    = 0x91C;
static const uintptr_t OFF_TEAM_NUM      = 0x3BF;

DWORD find_cs2_pid() {
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    auto snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t find_module_base(HANDLE hProcess, const wchar_t* name) {
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(hProcess, modules, sizeof(modules), &needed, LIST_MODULES_ALL))
        return 0;

    for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameExW(hProcess, modules[i], path, MAX_PATH)) {
            wchar_t* basename = wcsrchr(path, L'\\');
            if (basename) basename++;
            else basename = path;
            if (_wcsicmp(basename, name) == 0)
                return (uintptr_t)modules[i];
        }
    }
    return 0;
}

SIZE_T get_module_size(HANDLE hProcess, uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    SIZE_T read = 0;

    if (!ReadProcessMemory(hProcess, (LPCVOID)base, &dos, sizeof(dos), &read))
        return 0;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    if (!ReadProcessMemory(hProcess, (LPCVOID)(base + dos.e_lfanew), &nt, sizeof(nt), &read))
        return 0;
    if (nt.Signature != IMAGE_NT_SIGNATURE)
        return 0;

    return nt.OptionalHeader.SizeOfImage;
}

std::vector<uintptr_t> scan_all_signatures(HANDLE hProcess, uintptr_t base, SIZE_T size) {
    std::vector<uintptr_t> results;
    std::vector<BYTE> page(0x1000);

    for (uintptr_t addr = base; addr < base + size; addr += 0x1000) {
        SIZE_T read = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)addr, page.data(), 0x1000, &read))
            continue;
        if (read < 9) continue;

        for (size_t i = 0; i <= read - 9; i++) {
            if (page[i] != 0x48 || page[i + 1] != 0x8B || page[i + 2] != 0x05)
                continue;
            if (page[i + 7] != 0x41 || page[i + 8] != 0x89)
                continue;
            int32_t disp = *(int32_t*)(page.data() + i + 3);
            uintptr_t rip = addr + i + 7;
            uintptr_t global = rip + disp;
            results.push_back(global);
        }
    }
    return results;
}

template<typename T>
bool read(HANDLE hProcess, uintptr_t addr, T& out) {
    SIZE_T r = 0;
    return ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(T), &r) && r == sizeof(T);
}

int main() {
    std::cout << "[*] Finding cs2.exe..." << std::endl;
    DWORD pid = find_cs2_pid();
    if (!pid) {
        std::cerr << "[-] cs2.exe not found!" << std::endl;
        return 1;
    }
    std::cout << "[+] cs2.exe PID: " << pid << std::endl;

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] OpenProcess failed: " << GetLastError() << std::endl;
        return 1;
    }

    uintptr_t client_base = find_module_base(hProcess, L"client.dll");
    if (!client_base) {
        std::cerr << "[-] client.dll not found!" << std::endl;
        return 1;
    }
    std::cout << "[+] client.dll: " << std::hex << client_base << std::endl;

    SIZE_T client_size = get_module_size(hProcess, client_base);
    if (!client_size) {
        std::cerr << "[-] Failed to read client.dll size" << std::endl;
        return 1;
    }

    std::cout << "[*] Scanning for dwLocalPlayerController signature..." << std::endl;
    auto globals = scan_all_signatures(hProcess, client_base, client_size);
    std::cout << "[+] Found " << std::dec << globals.size() << " match(es)" << std::endl;

    uintptr_t controller = 0;
    uintptr_t global_addr = 0;

    for (size_t g = 0; g < globals.size(); g++) {
        uintptr_t cand = 0;
        if (read(hProcess, globals[g], cand) && cand && cand > 0x10000) {
            uint8_t test = 0;
            if (read(hProcess, cand, test)) {
                global_addr = globals[g];
                controller = cand;
                std::cout << "[+] Using candidate [" << g << "] Global: " << std::hex << global_addr << std::endl;
                break;
            }
        }
    }

    if (!controller) {
        std::cerr << "[-] No valid controller found!" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }
    std::cout << "[+] Controller: " << std::hex << controller << std::endl;

    uint8_t alive = 0, team = 0;
    uint32_t health = 0;
    int32_t armor = 0;

    read(hProcess, controller + OFF_PAWN_IS_ALIVE, alive);
    read(hProcess, controller + OFF_PAWN_HEALTH, health);
    read(hProcess, controller + OFF_PAWN_ARMOR, armor);
    read(hProcess, controller + OFF_TEAM_NUM, team);

    std::cout << "\n=== Local Player Data ===" << std::endl;
    std::cout << "[+] m_bPawnIsAlive (0x" << std::hex << OFF_PAWN_IS_ALIVE << "): " << std::dec << (int)alive << std::endl;
    std::cout << "[+] m_iPawnHealth  (0x" << std::hex << OFF_PAWN_HEALTH << "): " << std::dec << health << std::endl;
    std::cout << "[+] m_iPawnArmor   (0x" << std::hex << OFF_PAWN_ARMOR << "): " << std::dec << armor << std::endl;
    std::cout << "[+] m_iTeamNum     (0x" << std::hex << OFF_TEAM_NUM << "): " << std::dec << team << " ";
    if (team == 2) std::cout << "(CT)";
    else if (team == 3) std::cout << "(T)";
    std::cout << std::endl;

    if (health >= 1 && health <= 200) {
        std::cout << "\n[SUCCESS] Health value " << health << " is valid!" << std::endl;
    } else {
        std::cout << "\n[WARN] Health " << health << " is outside expected range [1-200]" << std::endl;
    }

    std::cout << "\n[*] Press Enter to stop (reading HP every 500ms)..." << std::endl;
    std::cin.get();

    while (true) {
        uintptr_t ctrl = 0;
        read(hProcess, global_addr, ctrl);
        if (!ctrl) { std::cout << "Controller NULL" << std::endl; break; }

        uint32_t h = 0;
        uint8_t a = 0;
        read(hProcess, ctrl + OFF_PAWN_HEALTH, h);
        read(hProcess, ctrl + OFF_PAWN_IS_ALIVE, a);
        std::cout << "\r  HP: " << std::dec << h << " | Alive: " << (int)a << "   " << std::flush;
        Sleep(500);
    }
    CloseHandle(hProcess);
    return 0;
}
