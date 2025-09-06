#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include "client_dll.hpp"
#include "offsets.hpp"
#include <iostream>
#include <cmath>

struct Vec3 {
    float x, y, z;
};

// 计算 3D 距离
float Get3DDistance(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Config {
    int Delay = 5;
    int UpDelay = 50;       // ms
    int aimKey = VK_XBUTTON2;
};
Config cfg;

struct KeyItem { int vk; std::wstring name; };
std::vector<KeyItem> keyItems = {
    { VK_LBUTTON, L"Right Mouse button" }, { VK_RBUTTON, L"Left Mouse button" }, { VK_MBUTTON, L"Mid Mouse button" },
    { VK_XBUTTON1, L"Mouse side button1" }, { VK_XBUTTON2, L"Mouse side button2"},{ VK_LMENU, L"Left ALT" },{ VK_CONTROL, L"Left Ctrl" },
    { 'Q', L"Q" }, { 'E', L"E" }, { 'R', L"R" }, { 'T', L"T" },
    { 'F', L"F" }, { 'G', L"G" },
    { 'Z', L"Z" }, { 'X', L"X" }, { 'C', L"C" }, { 'V', L"V" }, { 'B', L"B" }
};
/*
std::vector<KeyItem> keyItems = {
    { VK_LBUTTON, L"Right Mouse button" }, { VK_RBUTTON, L"Left Mouse button" }, { VK_MBUTTON, L"Mid Mouse button" },
    { VK_XBUTTON1, L"Mouse side button1" }, { VK_XBUTTON2, L"Mouse side button2"},{ VK_LMENU, L"Left ALT" },{ VK_CONTROL, L"Left Ctrl" },
    { 'Q', L"Q" }, { 'E', L"E" }, { 'R', L"R" }, { 'T', L"T" },
    { 'F', L"F" }, { 'G', L"G" },
    { 'Z', L"Z" }, { 'X', L"X" }, { 'C', L"C" }, { 'V', L"V" }, { 'B', L"B" }
};
* 
std::vector<KeyItem> keyItems = {
    { VK_LBUTTON, L"鼠标左键" }, { VK_RBUTTON, L"鼠标右键" }, { VK_MBUTTON, L"鼠标中键" },
    { VK_XBUTTON1, L"鼠标侧键1" }, { VK_XBUTTON2, L"鼠标侧键2" },
    { 'Q', L"Q" }, { 'W', L"W" }, { 'E', L"E" }, { 'R', L"R" }, { 'T', L"T" },
    { 'A', L"A" }, { 'S', L"S" }, { 'D', L"D" }, { 'F', L"F" }, { 'G', L"G" },
    { 'Z', L"Z" }, { 'X', L"X" }, { 'C', L"C" }, { 'V', L"V" }, { 'B', L"B" }
};
*/
HWND hwndMain = nullptr;
HWND hEditDelay = nullptr;
HWND hComboKeys = nullptr;

static uintptr_t ReadMemory(HANDLE hProcess, uintptr_t address) {
    uintptr_t buffer = 0;
    ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(buffer), nullptr);
    return buffer;
}

int GetWeaponId(HANDLE hProc, uintptr_t PlayerPawn) {
    if (!PlayerPawn) return 0;

    // 读取 m_pClippingWeapon
    uintptr_t clipping_weapon = 0;
    ReadProcessMemory(hProc,
        (LPCVOID)(PlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_pClippingWeapon),
        &clipping_weapon,
        sizeof(clipping_weapon),
        nullptr);

    if (!clipping_weapon) return 1;

    // 读取 weapon index
    USHORT weaponindex = 0;
    ReadProcessMemory(hProc,
        (LPCVOID)(clipping_weapon
            + cs2_dumper::schemas::client_dll::C_EconEntity::m_AttributeManager
            + cs2_dumper::schemas::client_dll::C_AttributeContainer::m_Item
            + cs2_dumper::schemas::client_dll::C_EconItemView::m_iItemDefinitionIndex),
        &weaponindex,
        sizeof(weaponindex),
        nullptr);

    if (!weaponindex) return 2;

    return weaponindex;
}


static uintptr_t GetBaseEntityFromHandle(HANDLE hProcess, uint32_t uHandle, uintptr_t clientBase) {
    uintptr_t entListBase = ReadMemory(hProcess, clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
    if (!entListBase) return 0;
    const int nIndex = uHandle & 0x7FFF;
    uintptr_t entityListBase = ReadMemory(hProcess, entListBase + 8 * (nIndex >> 9) + 16);
    if (!entityListBase) return 0;
    return ReadMemory(hProcess, entityListBase + 0x78 * (nIndex & 0x1FF));
}

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 modEntry;
    modEntry.dwSize = sizeof(modEntry);
    if (!Module32First(hSnap, &modEntry)) { CloseHandle(hSnap); return 0; }
    do {
        if (_wcsicmp(modEntry.szModule, modName) == 0) {
            CloseHandle(hSnap);
            return (uintptr_t)modEntry.modBaseAddr;
        }
    } while (Module32Next(hSnap, &modEntry));
    CloseHandle(hSnap);
    return 0;
}

void UpdateConfigFromControls() {
   
    wchar_t buf[16] = { 0 };
    GetWindowText(hEditDelay, buf, 16);
    int delay = _wtoi(buf);
    if (delay > 0) cfg.UpDelay = delay;

    int sel = SendMessage(hComboKeys, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < keyItems.size()) {
        cfg.aimKey = keyItems[sel].vk;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;
    case WM_COMMAND:
        if ((HWND)lParam == hEditDelay && HIWORD(wParam) == EN_CHANGE) {
            UpdateConfigFromControls();
        }
        if ((HWND)lParam == hComboKeys && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdateConfigFromControls();
        }
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"2025-9-6";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClass(&wc)) return false;

    hwndMain = CreateWindow(L"2025-9-6", L"Build 2025-9-6",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        100, 100, 270, 120,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwndMain) return false;

    hEditDelay = CreateWindow(L"EDIT", std::to_wstring(cfg.UpDelay).c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        10, 10, 40, 25,
        hwndMain, nullptr, hInstance, nullptr);

    hEditDelay = CreateWindow(L"EDIT", std::to_wstring(cfg.Delay).c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        55, 10, 40, 25,
        hwndMain, nullptr, hInstance, nullptr);

    hComboKeys = CreateWindow(L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        100, 10, 150, 200,
        hwndMain, nullptr, hInstance, nullptr);
    //Continuous/Delay     Trigger key\nCS2TB BY Github/Akaline-tech
    //开火持续时间/延迟射击 触发按键\n时间单位为毫秒
    HWND hStatic = CreateWindow(L"STATIC", L"Continuous/Delay     Trigger key\nCS2TB BY Github/Akaline-tech",
        WS_CHILD | WS_VISIBLE,
        10, 40, 250, 45,
        hwndMain, nullptr, hInstance, nullptr);

    for (size_t i = 0; i < keyItems.size(); i++) {
        SendMessage(hComboKeys, CB_ADDSTRING, 0, (LPARAM)keyItems[i].name.c_str());
        if (keyItems[i].vk == cfg.aimKey)
            SendMessage(hComboKeys, CB_SETCURSEL, (WPARAM)i, 0);
    }

    return true;
}

void TriggerBotThread() {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 procEntry; procEntry.dwSize = sizeof(procEntry);

    if (Process32First(hSnap, &procEntry)) {
        do {
            if (_wcsicmp(procEntry.szExeFile, L"cs2.exe") == 0) {
                procId = procEntry.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnap, &procEntry));
    }
    CloseHandle(hSnap);
    if (!procId) return;

    HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_OPERATION, FALSE, procId);
    if (!hProc) return;

    uintptr_t clientBase = GetModuleBaseAddress(procId, L"client.dll");
    if (!clientBase) return;

    while (true) {
        uintptr_t localPawn = ReadMemory(hProc, clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        if (!localPawn) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        int localHealth = 0;
        ReadProcessMemory(hProc, (LPCVOID)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth), &localHealth, sizeof(localHealth), nullptr);
        if (localHealth <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        int localTeam = 0;
        ReadProcessMemory(hProc, (LPCVOID)(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum), &localTeam, sizeof(localTeam), nullptr);

        uint32_t cross = 0;
        ReadProcessMemory(hProc, (LPCVOID)(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex), &cross, sizeof(cross), nullptr);
        if (cross == 0 || cross == 0xFFFFFFFF) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        auto playerpawn = GetBaseEntityFromHandle(hProc, cross, clientBase);
        if (!playerpawn) continue;

        int playerteam = 0;
        ReadProcessMemory(hProc, (LPCVOID)(playerpawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum), &playerteam, sizeof(playerteam), nullptr);
        if (playerteam == localTeam) continue;

        int playerHealth = 0;
        ReadProcessMemory(hProc, (LPCVOID)(playerpawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth), &playerHealth, sizeof(playerHealth), nullptr);
        if (playerHealth == 0) continue;

        Vec3 localPos, enemyPos;
        ReadProcessMemory(hProc, (LPCVOID)(localPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin), &localPos, sizeof(localPos), nullptr);
        ReadProcessMemory(hProc, (LPCVOID)(playerpawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin), &enemyPos, sizeof(enemyPos), nullptr);

        int weaponId = GetWeaponId(hProc, localPawn);

        if ((GetAsyncKeyState(cfg.aimKey) & 0x8000)) {
            if (weaponId != 59 && weaponId != 42 && weaponId != 507) {
                INPUT input = { 0 };
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.Delay));
                SendInput(1, &input, sizeof(INPUT));

                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.UpDelay));
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));
            }
            else if (Get3DDistance(localPos, enemyPos) < 85) {
                Sleep(5);
                INPUT input = { 0 };
                input.type = INPUT_MOUSE;

                if (playerHealth <= 65 && playerHealth >= 25 && Get3DDistance(localPos, enemyPos) <= 71) {
                    // 右键攻击
                    input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                    Sleep(5);
                    input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                    SendInput(1, &input, sizeof(INPUT));
                }
                else {
                    // 左键攻击
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                    Sleep(5);
                    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &input, sizeof(INPUT));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // CPU 减负
    }
    CloseHandle(hProc);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (!CreateMainWindow(hInstance, nCmdShow)) return 1;

    std::thread t(TriggerBotThread);
    t.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
