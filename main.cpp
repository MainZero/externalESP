// language: C++, file: main.cpp, target: Windows 11, MSVC x64
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
// 1. MEMORY MANAGER
// ============================================================
class Memory {
private:
    DWORD pid = 0;
    HANDLE process = nullptr;

    DWORD GetProcessIdByName(const wchar_t* name) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        DWORD targetPid = 0;
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (!_wcsicmp(pe.szExeFile, name)) {
                    targetPid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
        return targetPid;
    }

public:
    uintptr_t base_address = 0;

    bool attach_stealth(const wchar_t* process_name) {
        pid = GetProcessIdByName(process_name);
        if (pid == 0) return false;

        DWORD explorerPid = GetProcessIdByName(L"explorer.exe");
        if (explorerPid == 0) {
            process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
            return process != nullptr;
        }

        HANDLE hTrusted = OpenProcess(PROCESS_DUP_HANDLE, FALSE, explorerPid);
        if (!hTrusted) return false;

        HANDLE hTarget = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (!hTarget) {
            CloseHandle(hTrusted);
            return false;
        }

        HANDLE hStealth = nullptr;
        if (!DuplicateHandle(hTrusted, hTarget, GetCurrentProcess(), &hStealth,
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hTrusted);
            CloseHandle(hTarget);
            return false;
        }

        CloseHandle(hTrusted);
        CloseHandle(hTarget);

        if (!DuplicateHandle(GetCurrentProcess(), hStealth, GetCurrentProcess(), &process,
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hStealth);
            return false;
        }

        CloseHandle(hStealth);
        return process != nullptr;
    }

    bool attach_direct(const wchar_t* process_name) {
        pid = GetProcessIdByName(process_name);
        if (pid == 0) return false;
        process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        return process != nullptr;
    }

    uintptr_t find_module(const char* module_name) {
        if (!process) return 0;
        HMODULE mods[1024];
        DWORD needed;
        if (EnumProcessModulesEx(process, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
            for (unsigned i = 0; i < needed / sizeof(HMODULE); i++) {
                char name[MAX_PATH];
                if (GetModuleBaseNameA(process, mods[i], name, MAX_PATH)) {
                    if (strstr(name, module_name)) {
                        return (uintptr_t)mods[i];
                    }
                }
            }
        }
        return 0;
    }

    template <typename T>
    T read(uintptr_t address) {
        T buffer{};
        if (address > 0x10000) {
            ReadProcessMemory(process, (LPCVOID)address, &buffer, sizeof(T), nullptr);
        }
        return buffer;
    }
};

// ============================================================
// 2. OVERLAY
// ============================================================
Memory mem;
HWND overlay_window = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void render_overlay() {
    if (!overlay_window) return;
    HDC hdc = GetDC(overlay_window);
    if (!hdc) return;

    RECT rect;
    GetClientRect(overlay_window, &rect);
    FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 255, 0));

    std::stringstream ss;
    ss << "libil2cpp.so Base: 0x" << std::hex << std::uppercase << mem.base_address;
    std::string text = ss.str();

    TextOutA(hdc, 10, 10, text.c_str(), (int)text.length());
    TextOutA(hdc, 10, 30, "External ESP Active (Stealth Mode)", 31);

    ReleaseDC(overlay_window, hdc);
}

// ============================================================
// 3. MAIN
// ============================================================
int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    bool attached = false;

    // 1. Try LDPlayer 9 headless process (most common)
    if (mem.attach_stealth(L"Ld9BoxHeadless.exe")) {
        attached = true;
    }
    // 2. Try older LDPlayer headless process
    else if (mem.attach_stealth(L"LdVBoxHeadless.exe")) {
        attached = true;
    }
    // 3. Fallback to direct open (less stealthy, but works if hijack is blocked)
    else if (mem.attach_direct(L"Ld9BoxHeadless.exe")) {
        attached = true;
    }
    else if (mem.attach_direct(L"LdVBoxHeadless.exe")) {
        attached = true;
    }
    else if (mem.attach_direct(L"dnplayer.exe")) {
        // Last resort: try dnplayer, though it often fails
        attached = true;
    }

    if (!attached) {
        MessageBoxA(nullptr, "Failed to attach. Open Task Manager -> Details tab and check if Ld9BoxHeadless.exe or LdVBoxHeadless.exe is running.", "Error", MB_ICONERROR);
        return 1;
    }

    // Find libil2cpp.so
    mem.base_address = mem.find_module("libil2cpp.so");
    if (!mem.base_address) {
        MessageBoxA(nullptr, "Attached, but could not find libil2cpp.so. Is the game fully running in the lobby?", "Error", MB_ICONERROR);
        return 1;
    }

    // Create overlay
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "StealthESP";
    RegisterClassA(&wc);

    HWND ld_window = FindWindowA(nullptr, "LDPlayer");
    if (!ld_window) ld_window = FindWindowA(nullptr, "雷电模拟器");
    if (!ld_window) {
        MessageBoxA(nullptr, "Could not find LDPlayer window.", "Error", MB_ICONERROR);
        return 1;
    }

    RECT rect;
    GetWindowRect(ld_window, &rect);

    overlay_window = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "StealthESP", "ESP",
        WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!overlay_window) {
        MessageBoxA(nullptr, "Failed to create overlay.", "Error", MB_ICONERROR);
        return 1;
    }

    SetLayeredWindowAttributes(overlay_window, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(overlay_window, SW_SHOW);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        render_overlay();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
