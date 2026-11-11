// language: C++, file: main.cpp, target: Windows 11, MSVC x64
// *external ESP matrix tester. prints a 4x4 grid to confirm the View Matrix.*

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

// ============================================================
// PUT YOUR ADDRESS HERE (e.g., 0x263DC844)
// ============================================================
uintptr_t TEST_MATRIX_ADDRESS = 0x263DC844; // <--- CHANGE THIS

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
        if (!hTarget) { CloseHandle(hTrusted); return false; }
        HANDLE hStealth = nullptr;
        if (!DuplicateHandle(hTrusted, hTarget, GetCurrentProcess(), &hStealth, PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hTrusted); CloseHandle(hTarget); return false;
        }
        CloseHandle(hTrusted); CloseHandle(hTarget);
        if (!DuplicateHandle(GetCurrentProcess(), hStealth, GetCurrentProcess(), &process, PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, 0)) {
            CloseHandle(hStealth); return false;
        }
        CloseHandle(hStealth);
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

Memory mem;
HWND overlay_window = nullptr;
HWND ld_window = nullptr;
float matrix[16];

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void update_game_data() {
    // Read the entire 4x4 matrix (16 floats = 64 bytes)
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
    
    // Draw the 4x4 Matrix on the screen
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

int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    if (!mem.attach_stealth(L"Ld9BoxHeadless.exe") && !mem.attach_stealth(L"LdVBoxHeadless.exe")) {
        MessageBoxA(nullptr, "Failed to attach. Run as Administrator.", "Error", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "StealthESP";
    RegisterClassA(&wc);

    ld_window = FindWindowA(nullptr, "LDPlayer");
    if (!ld_window) ld_window = FindWindowA(nullptr, "雷电模拟器");
    
    if (!ld_window) return 1;

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
