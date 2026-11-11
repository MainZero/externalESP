// language: C++, file: main.cpp, target: Windows 11, MSVC x64
// *external ESP matrix tester with SeDebugPrivilege elevation and detailed error reporting.*

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
// PUT YOUR ADDRESS HERE (e.g., 0x263DC844)
// ============================================================
uintptr_t TEST_MATRIX_ADDRESS = 0x263DC844; // <--- CHANGE THIS

// ============================================================
// 1. PRIVILEGE ESCALATION (THE FIX)
// ============================================================
bool EnableDebugPrivilege() {
    HANDLE hToken;
    LUID luid;
    TOKEN_PRIVILEGES tkp;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    return GetLastError() == ERROR_SUCCESS;
}

// ============================================================
// 2. MEMORY MANAGER
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
                if (!_wcsicmp(pe.szExeFile, name)) { targetPid = pe.th32ProcessID; break; }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
        return targetPid;
    }
public:
    bool attach_direct(const wchar_t* process_name) {
        pid = GetProcessIdByName(process_name);
        if (pid == 0) return false;
        
        // Open with full read access
        process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        return process != nullptr;
    }
    
    template <typename T>
    T read(uintptr_t address) {
        T buffer{};
        if (process && address > 0x10000) {
            ReadProcessMemory(process, (LPCVOID)address, &buffer, sizeof(T), nullptr);
        }
        return buffer;
    }
};

// ============================================================
// 3. GLOBAL VARIABLES & OVERLAY
// ============================================================
Memory mem;
HWND overlay_window = nullptr;
HWND ld_window = nullptr;
float matrix[16];

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void update_game_data() {
    for (int i = 0; i < 16; i++) {
        matrix[i] = mem.read<float>(TEST_MATRIX_ADDRESS + (i * 4));
    }
}

void render_overlay() {
    if (!overlay_window || !IsWindow(overlay_window)) return;
    HDC hdc = GetDC(overlay_window);
    if (!hdc) return;

    RECT rect;
    GetClientRect(overlay_window, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    FillRect(memDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(0, 255, 0));
    
    std::stringstream ss;
    for (int row = 0; row < 4; row++) {
        ss << "[" << std::fixed << std::setw(8) << std::setprecision(2) << matrix[row * 4 + 0] << "  "
           << std::setw(8) << matrix[row * 4 + 1] << "  "
           << std::setw(8) << matrix[row * 4 + 2] << "  "
           << std::setw(8) << matrix[row * 4 + 3] << "]\n";
    }
    
    TextOutA(memDC, 10, 10, ss.str().c_str(), (int)ss.str().length());
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(overlay_window, hdc);
}

// ============================================================
// 4. MAIN
// ============================================================
int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    // STEP 1: Enable Debug Privilege
    if (!EnableDebugPrivilege()) {
        MessageBoxA(nullptr, "Failed to elevate privileges. Run as Administrator.", "Error", MB_ICONERROR);
        return 1;
    }

    // STEP 2: Try to attach to both common LDPlayer processes
    bool attached = false;
    if (mem.attach_direct(L"Ld9BoxHeadless.exe")) attached = true;
    else if (mem.attach_direct(L"LdVBoxHeadless.exe")) attached = true;

    if (!attached) {
        // Get the exact Windows error code (5 = Access Denied, 87 = Invalid Parameter, 2 = Not Found)
        DWORD err = GetLastError();
        std::string msg = "Failed to attach to LDPlayer headless process.\n\n"
                          "Possible reasons:\n"
                          "1. LDPlayer 9 is not running.\n"
                          "2. The game is not loaded.\n"
                          "3. Windows Error Code: " + std::to_string(err);
        MessageBoxA(nullptr, msg.c_str(), "Attachment Failed", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "StealthESP";
    RegisterClassA(&wc);

    ld_window = FindWindowA(nullptr, "LDPlayer");
    if (!ld_window) ld_window = FindWindowA(nullptr, "雷电模拟器");
    
    if (!ld_window) {
        MessageBoxA(nullptr, "Attached to memory, but could not find the LDPlayer window.", "Error", MB_ICONERROR);
        return 1;
    }

    overlay_window = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "StealthESP", "ESP", WS_POPUP, 0, 0, 100, 100,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    SetLayeredWindowAttributes(overlay_window, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(overlay_window, SW_SHOW);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (IsWindow(ld_window) && IsWindowVisible(ld_window)) {
            RECT clientRect;
            GetClientRect(ld_window, &clientRect);
            POINT topLeft = { clientRect.left, clientRect.top };
            ClientToScreen(ld_window, &topLeft);
            MoveWindow(overlay_window, topLeft.x, topLeft.y, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
        }

        update_game_data();
        render_overlay();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); 
    }
    return 0;
}
