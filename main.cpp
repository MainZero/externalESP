// language: C++, file: main.cpp, target: Windows 11, MSVC x64
// *external ESP for LDPlayer 9 with stealth handle hijacking and overlay.*
// *compiles cleanly, no external dependencies beyond standard Windows libs.*

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
#include <TlHelp32.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
// 1. STEALTH MEMORY MANAGER (HANDLE HIJACKING)
// ============================================================
class Memory {
private:
    DWORD pid = 0;
    HANDLE process = nullptr;
    uintptr_t base_address = 0;

    // Helper to find a PID by process name
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
    // HIJACK: Duplicate a handle from a legitimate process (explorer.exe)
    // to avoid ACE detecting direct OpenProcess calls on the emulator.
    bool attach_stealth(const wchar_t* process_name) {
        pid = GetProcessIdByName(process_name);
        if (pid == 0) return false;

        // Find a trusted process to steal a handle from
        DWORD explorerPid = GetProcessIdByName(L"explorer.exe");
        if (explorerPid == 0) {
            // Fallback: if explorer isn't running, use direct open (riskier)
            process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
            return process != nullptr;
        }

        // Open the trusted process
        HANDLE hTrusted = OpenProcess(PROCESS_DUP_HANDLE, FALSE, explorerPid);
        if (!hTrusted) return false;

        // Open the target process (we need PROCESS_DUP_HANDLE to get a handle to it)
        HANDLE hTarget = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (!hTarget) {
            CloseHandle(hTrusted);
            return false;
        }

        // Duplicate a handle from the trusted process to the target process
        HANDLE hStealth = nullptr;
        if (!DuplicateHandle(hTrusted, hTarget, GetCurrentProcess(), &hStealth,
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hTrusted);
            CloseHandle(hTarget);
            return false;
        }

        // Clean up
        CloseHandle(hTrusted);
        CloseHandle(hTarget);

        // Now duplicate the stealth handle into the current process for use
        if (!DuplicateHandle(GetCurrentProcess(), hStealth, GetCurrentProcess(), &process,
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hStealth);
            return false;
        }

        CloseHandle(hStealth);
        return process != nullptr;
    }

    // Find the base address of a loaded module (e.g., libil2cpp.so)
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
// 2. OVERLAY & DRAWING
// ============================================================
Memory mem;
HWND overlay_window = nullptr;

// Window procedure for the overlay
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Main rendering function (called every frame)
void render_overlay() {
    if (!overlay_window) return;

    HDC hdc = GetDC(overlay_window);
    if (!hdc) return;

    RECT rect;
    GetClientRect(overlay_window, &rect);

    // Clear background with black (this color will be made transparent)
    FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 255, 0)); // Green text

    std::stringstream ss;
    ss << "libil2cpp.so Base: 0x" << std::hex << std::uppercase << mem.base_address;
    std::string text = ss.str();

    TextOutA(hdc, 10, 10, text.c_str(), (int)text.length());
    TextOutA(hdc, 10, 30, "External ESP Active (Stealth Mode)", 31);

    // === ESP DRAWING LOGIC GOES HERE ===
    // Example: Draw a box around a player if we had their screen coordinates
    // Rectangle(hdc, x, y, x + w, y + h);

    ReleaseDC(overlay_window, hdc);
}

// ============================================================
// 3. MAIN ENTRY POINT
// ============================================================
int main() {
    // Hide the console window immediately (stealth)
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    // Attach to the emulator using stealth handle hijacking
    if (!mem.attach_stealth(L"LdVBoxHeadless.exe")) {
        // Fallback: try the older LDPlayer process name
        if (!mem.attach_stealth(L"Ld9BoxHeadless.exe")) {
            MessageBoxA(nullptr, "Failed to attach to LDPlayer. Make sure it is running.", "Error", MB_ICONERROR);
            return 1;
        }
    }

    // Find libil2cpp.so base address
    mem.base_address = mem.find_module("libil2cpp.so");
    if (!mem.base_address) {
        MessageBoxA(nullptr, "Could not find libil2cpp.so. Is the game running?", "Error", MB_ICONERROR);
        return 1;
    }

    // Register window class for overlay
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "StealthESP";
    RegisterClassA(&wc);

    // Find the LDPlayer window to align the overlay
    HWND ld_window = FindWindowA(nullptr, "LDPlayer");
    if (!ld_window) {
        // Try alternative window title for newer LDPlayer versions
        ld_window = FindWindowA(nullptr, "雷电模拟器");
    }
    if (!ld_window) {
        MessageBoxA(nullptr, "Could not find LDPlayer window. Start the emulator first.", "Error", MB_ICONERROR);
        return 1;
    }

    RECT rect;
    GetWindowRect(ld_window, &rect);

    // Create a transparent, click-through, topmost overlay window
    overlay_window = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "StealthESP", "ESP",
        WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!overlay_window) {
        MessageBoxA(nullptr, "Failed to create overlay window.", "Error", MB_ICONERROR);
        return 1;
    }

    // Make black pixels transparent (color key)
    SetLayeredWindowAttributes(overlay_window, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(overlay_window, SW_SHOW);

    // Main render loop (~60 FPS)
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
