// language: C++, file: main.cpp, target: Windows 11, MSVC x64
// *FINAL AUTOMATED EXTERNAL ESP for CFL (Unity 5.6.4).*
// *Scans for View Matrix and Entity List automatically using structural heuristics.*

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
#include <cmath>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
// 1. MATH & STRUCTS
// ============================================================
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

bool WorldToScreen(Vec3 pos, Vec2& screen, float matrix[16], int windowWidth, int windowHeight) {
    float clipX = pos.x * matrix[0] + pos.y * matrix[4] + pos.z * matrix[8] + matrix[12];
    float clipY = pos.x * matrix[1] + pos.y * matrix[5] + pos.z * matrix[9] + matrix[13];
    float clipW = pos.x * matrix[3] + pos.y * matrix[7] + pos.z * matrix[11] + matrix[15];

    if (clipW < 0.1f) return false;

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;

    screen.x = (windowWidth / 2.0f) * (ndcX + 1.0f);
    screen.y = (windowHeight / 2.0f) * (1.0f - ndcY);

    return true;
}

// ============================================================
// 2. PRIVILEGE ESCALATION & MEMORY MANAGER
// ============================================================
bool EnableDebugPrivilege() {
    HANDLE hToken; LUID luid; TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) { CloseHandle(hToken); return false; }
    tkp.PrivilegeCount = 1; tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL)) { CloseHandle(hToken); return false; }
    CloseHandle(hToken); return GetLastError() == ERROR_SUCCESS;
}

class Memory {
public:
    DWORD pid = 0; HANDLE process = nullptr;
    uintptr_t module_base = 0;
    size_t module_size = 0;

    DWORD GetProcessIdByName(const wchar_t* name) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) }; DWORD targetPid = 0;
        if (Process32FirstW(snapshot, &pe)) {
            do { if (!_wcsicmp(pe.szExeFile, name)) { targetPid = pe.th32ProcessID; break; } } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot); return targetPid;
    }

    bool attach_direct(const wchar_t* process_name) {
        pid = GetProcessIdByName(process_name);
        if (pid == 0) return false;
        process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        return process != nullptr;
    }
    
    template <typename T> T read(uintptr_t address) {
        T buffer{};
        if (process && address > 0x10000) ReadProcessMemory(process, (LPCVOID)address, &buffer, sizeof(T), nullptr);
        return buffer;
    }

    // Finds the exact address of libil2cpp.so
    void find_libil2cpp() {
        MEMORY_BASIC_INFORMATION mbi; uintptr_t address = 0;
        while (VirtualQueryEx(process, (LPCVOID)address, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
                std::vector<BYTE> buffer(mbi.RegionSize); SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytes_read)) {
                    std::string haystack((char*)buffer.data(), bytes_read);
                    if (haystack.find("libil2cpp.so") != std::string::npos) {
                        module_base = (uintptr_t)mbi.AllocationBase;
                        module_size = mbi.RegionSize;
                        return;
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
    }

    // 1. Auto-Scan for View Matrix
    uintptr_t ScanForViewMatrix() {
        if (!process) return 0;
        MEMORY_BASIC_INFORMATION mbi; uintptr_t address = 0;
        std::vector<uintptr_t> candidates;

        while (VirtualQueryEx(process, (LPCVOID)address, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
                std::vector<BYTE> buffer(mbi.RegionSize); SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytes_read)) {
                    for (size_t i = 0; i <= bytes_read - sizeof(float) * 16; i += 4) {
                        float* m = (float*)&buffer[i];
                        if (m[15] == 1.0f && m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f) {
                            if (std::abs(m[0]) <= 1.5f && std::abs(m[5]) <= 1.5f && std::abs(m[10]) <= 1.5f) {
                                if (m[12] != 0.0f && m[13] != 0.0f && m[14] != 0.0f) {
                                    if (std::abs(m[0]) < 2.0f && std::abs(m[12]) < 15000.0f) {
                                        candidates.push_back((uintptr_t)mbi.BaseAddress + i);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
        if (candidates.empty()) return 0;
        std::vector<std::pair<uintptr_t, float>> first_reads;
        for (auto addr : candidates) first_reads.push_back({addr, read<float>(addr)});
        Sleep(500); 
        for (auto& pair : first_reads) {
            float new_val = read<float>(pair.first);
            if (new_val != pair.second && std::abs(new_val) <= 1.5f) return pair.first; 
        }
        return candidates[0];
    }

    // 2. Heuristic Entity List Scanner (Finds the array of players)
    uintptr_t ScanForEntityList() {
        if (!process) return 0;
        MEMORY_BASIC_INFORMATION mbi; uintptr_t address = 0;

        while (VirtualQueryEx(process, (LPCVOID)address, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
                std::vector<BYTE> buffer(mbi.RegionSize); SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytes_read)) {
                    // Scan for arrays of 10+ pointers
                    for (size_t i = 0; i <= bytes_read - (sizeof(uintptr_t) * 10); i += 8) {
                        uintptr_t* ptrArray = (uintptr_t*)&buffer[i];
                        int validCount = 0;
                        for (int j = 0; j < 10; j++) {
                            if (ptrArray[j] > 0x10000 && ptrArray[j] < 0x7FFFFFFF0000) validCount++;
                        }

                        // If we found a list of 10 valid pointers, check if the first one points to a valid Vector3
                        if (validCount >= 10) {
                            for (int offset : {0x38, 0x50, 0x60, 0x90}) {
                                Vec3 pos = read<Vec3>(ptrArray[0] + offset);
                                if (std::abs(pos.x) > 0.1f && std::abs(pos.x) < 20000.0f &&
                                    std::abs(pos.y) > 0.1f && std::abs(pos.y) < 20000.0f &&
                                    std::abs(pos.z) > 0.1f && std::abs(pos.z) < 20000.0f) {
                                    return (uintptr_t)mbi.BaseAddress + i;
                                }
                            }
                        }
                    }
                }
            }
            address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
        return 0;
    }
};

// ============================================================
// 3. OVERLAY & RENDER
// ============================================================
Memory mem; HWND overlay_window = nullptr; HWND ld_window = nullptr;
uintptr_t viewMatrixAddress = 0; uintptr_t entityListAddress = 0;
float viewMatrix[16]; bool isScanning = true;

struct WindowData { DWORD targetPid; HWND hwnd; };
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowData* data = (WindowData*)lParam; DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->targetPid && IsWindowVisible(hwnd)) { if (GetWindowTextLengthA(hwnd) > 0) { data->hwnd = hwnd; return FALSE; } }
    return TRUE;
}
HWND GetEmulatorWindow() {
    DWORD uiPid = 0; HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(snapshot, &pe)) { do { if (!_wcsicmp(pe.szExeFile, L"dnplayer.exe")) { uiPid = pe.th32ProcessID; break; } } while (Process32NextW(snapshot, &pe)); }
    CloseHandle(snapshot); if (uiPid == 0) return nullptr; WindowData data = { uiPid, nullptr }; EnumWindows(EnumWindowsProc, (LPARAM)&data); return data.hwnd;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void render_overlay() {
    if (!overlay_window || !IsWindow(overlay_window)) return;
    HDC hdc = GetDC(overlay_window); if (!hdc) return;
    RECT rect; GetClientRect(overlay_window, &rect); int width = rect.right - rect.left; int height = rect.bottom - rect.top;

    HDC memDC = CreateCompatibleDC(hdc); HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    FillRect(memDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(memDC, TRANSPARENT); SetTextColor(memDC, RGB(0, 255, 0));

    std::stringstream ss;
    if (isScanning) { ss << "SCANNING FOR MATRIX AND ENTITIES... PLEASE WAIT..."; }
    else if (viewMatrixAddress == 0) { ss << "FAILED TO FIND VIEW MATRIX."; }
    else {
        for (int i = 0; i < 16; i++) viewMatrix[i] = mem.read<float>(viewMatrixAddress + (i * 4));
        
        ss << "View Matrix: 0x" << std::hex << std::uppercase << viewMatrixAddress << "\n";
        if (entityListAddress != 0) {
            ss << "Entity List: 0x" << std::hex << std::uppercase << entityListAddress << " (FOUND!)\n\n" << std::dec;
            
            // DRAW ENTITIES
            for (int i = 0; i < 64; i++) {
                uintptr_t playerPtr = mem.read<uintptr_t>(entityListAddress + (i * 8));
                if (!playerPtr || playerPtr < 0x10000) continue;

                Vec3 pos = {0,0,0}; int usedOffset = 0;
                for (int offset : {0x38, 0x50, 0x60, 0x90}) {
                    pos = mem.read<Vec3>(playerPtr + offset);
                    if (std::abs(pos.x) > 0.1f && std::abs(pos.x) < 20000.0f) { usedOffset = offset; break; }
                }
                
                if (usedOffset != 0) {
                    Vec2 screen;
                    if (WorldToScreen(pos, screen, viewMatrix, width, height)) {
                        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0)); SelectObject(memDC, pen);
                        HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
                        Rectangle(memDC, (int)screen.x - 20, (int)screen.y - 50, (int)screen.x + 20, (int)screen.y + 10);
                        SelectObject(memDC, oldBrush); DeleteObject(pen);
                        
                        std::stringstream pText; pText << i << ": " << (int)pos.x << "," << (int)pos.y << "," << (int)pos.z << " (Off:0x" << std::hex << usedOffset << ")";
                        TextOutA(memDC, (int)screen.x - 20, (int)screen.y - 65, pText.str().c_str(), pText.str().length());
                    }
                }
            }
        } else { ss << "Entity List: Searching...\n"; }
    }
    TextOutA(memDC, 10, 10, ss.str().c_str(), (int)ss.str().length());
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap); DeleteObject(memBitmap); DeleteDC(memDC); ReleaseDC(overlay_window, hdc);
}

// ============================================================
// 4. MAIN
// ============================================================
int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    if (!EnableDebugPrivilege()) return 1;
    if (!mem.attach_direct(L"Ld9BoxHeadless.exe") && !mem.attach_direct(L"LdVBoxHeadless.exe")) return 1;

    WNDCLASSA wc = { 0 }; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = "StealthESP";
    RegisterClassA(&wc); ld_window = GetEmulatorWindow(); if (!ld_window) return 1;

    overlay_window = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "StealthESP", "ESP", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(overlay_window, RGB(0, 0, 0), 0, LWA_COLORKEY); ShowWindow(overlay_window, SW_SHOW);

    std::thread scanner([]() {
        mem.find_libil2cpp();
        viewMatrixAddress = mem.ScanForViewMatrix();
        if (viewMatrixAddress != 0) entityListAddress = mem.ScanForEntityList();
        isScanning = false;
    });
    scanner.detach();

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        if (GetAsyncKeyState(VK_END) & 0x8000) { DestroyWindow(overlay_window); break; }
        if (IsWindow(ld_window) && IsWindowVisible(ld_window)) {
            RECT clientRect; GetClientRect(ld_window, &clientRect); POINT topLeft = { clientRect.left, clientRect.top };
            ClientToScreen(ld_window, &topLeft); MoveWindow(overlay_window, topLeft.x, topLeft.y, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
        }
        render_overlay(); std::this_thread::sleep_for(std::chrono::milliseconds(16)); 
    }
    return 0;
}
