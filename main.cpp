// language: C++, file: main.cpp, target: Windows 11, MSVC x64
// *external ESP for LDPlayer 9 with stealth attachment, ELF memory scanning, and overlay.*
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

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// Define a macro for all standard readable memory protections
#ifndef PAGE_READABLE
#define PAGE_READABLE (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
#endif

// ============================================================
// 1. MEMORY MANAGER (Stealth + ELF Scanner)
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

    // Scans memory for the Android ELF header and the module name
    uintptr_t find_module_elf(const char* module_name) {
        if (!process) return 0;

        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t address = 0;
        
        while (VirtualQueryEx(process, (LPCVOID)address, &mbi, sizeof(mbi))) {
            // Ensure memory is committed, readable, and not guarded (avoids access violations)
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READABLE) && !(mbi.Protect & PAGE_GUARD)) {
                size_t read_size = (mbi.RegionSize < 0x10000) ? mbi.RegionSize : 0x10000;
                std::vector<BYTE> buffer(read_size);
                SIZE_T bytes_read = 0;
                
                if (ReadProcessMemory(process, mbi.BaseAddress, buffer.data(), read_size, &bytes_read) && bytes_read > 4) {
                    // Check for ELF magic header (0x7F 'E' 'L' 'F')
                    if (buffer[0] == 0x7F && buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F') {
                        // Check if the module name exists in the first 64KB
                        std::string haystack((char*)buffer.data(), bytes_read);
                        if (haystack.find(module_name) != std::string::npos) {
                            return (uintptr_t)mbi.BaseAddress;
                        }
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
        return 0;
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
// 2. OVERLAY & DRAWING (Double-Buffered)
// ============================================================
Memory mem;
HWND overlay_window = nullptr;
HWND ld_window = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void render_overlay() {
    if (!overlay_window || !IsWindow(overlay_window)) return;

    HDC hdc = GetDC(overlay_window);
    if (!hdc) return;

    RECT rect;
    GetClientRect(overlay_window, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Double Buffering (Prevents flickering)
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Clear background with black (transparent color key)
    FillRect(memDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    
    // Draw Text
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(0, 255, 0));
    
    std::stringstream ss;
    ss << "libil2cpp.so Base: 0x" << std::hex << std::uppercase << mem.base_address;
    std::string text = ss.str();

    TextOutA(memDC, 10, 10, text.c_str(), (int)text.length());
    TextOutA(memDC, 10, 30, "External ESP Active (Double-Buffered)", 37);

    // Push to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(overlay_window, hdc);
}

// ============================================================
// 3. MAIN ENTRY POINT
// ============================================================
int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    bool attached = false;
    if (mem.attach_stealth(L"Ld9BoxHeadless.exe")) attached = true;
    else if (mem.attach_stealth(L"LdVBoxHeadless.exe")) attached = true;
    else if (mem.attach_direct(L"Ld9BoxHeadless.exe")) attached = true;
    else if (mem.attach_direct(L"LdVBoxHeadless.exe")) attached = true;

    if (!attached) {
        MessageBoxA(nullptr, "Failed to attach. Check Task Manager for Ld9BoxHeadless.exe and run as Administrator.", "Attachment Failed", MB_ICONERROR);
        return 1;
    }

    mem.base_address = mem.find_module_elf("libil2cpp.so");
    if (!mem.base_address) {
        MessageBoxA(nullptr, "Could not find libil2cpp.so. Is the game in the lobby?", "Module Not Found", MB_ICONERROR);
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
        MessageBoxA(nullptr, "Could not find LDPlayer window.", "Error", MB_ICONERROR);
        return 1;
    }

    overlay_window = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "StealthESP", "ESP", WS_POPUP,
        0, 0, 100, 100, // Initial size, auto-aligned in loop
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

        // Auto-align overlay exactly to the game screen
        if (IsWindow(ld_window) && IsWindowVisible(ld_window)) {
            RECT clientRect;
            GetClientRect(ld_window, &clientRect);
            POINT topLeft = { clientRect.left, clientRect.top };
            ClientToScreen(ld_window, &topLeft);
            
            int w = clientRect.right - clientRect.left;
            int h = clientRect.bottom - clientRect.top;

            MoveWindow(overlay_window, topLeft.x, topLeft.y, w, h, TRUE);
        }

        render_overlay();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    return 0;
}
