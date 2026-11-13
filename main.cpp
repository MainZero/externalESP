// language: C++, file: internal_scanner.cpp, target: Windows 11, MSVC x64
// *Internal IL2CPP Offset Scanner for LDPlayer.*
// *Injects into the game, asks the engine for exact offsets, and writes them to a text file.*

#include <windows.h>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// 1. IL2CPP EXPORTS
// ============================================================
typedef void* (*t_il2cpp_domain_get)();
typedef void** (*t_il2cpp_domain_get_assemblies)(void* domain, size_t* count);
typedef void* (*t_il2cpp_assembly_get_image)(void* assembly);
typedef void* (*t_il2cpp_class_from_name)(void* image, const char* namespaze, const char* name);
typedef void* (*t_il2cpp_class_get_field_from_name)(void* klass, const char* name);
typedef size_t (*t_il2cpp_field_get_offset)(void* field);
typedef const char* (*t_il2cpp_class_get_name)(void* klass);

t_il2cpp_domain_get il2cpp_domain_get = nullptr;
t_il2cpp_domain_get_assemblies il2cpp_domain_get_assemblies = nullptr;
t_il2cpp_assembly_get_image il2cpp_assembly_get_image = nullptr;
t_il2cpp_class_from_name il2cpp_class_from_name = nullptr;
t_il2cpp_class_get_field_from_name il2cpp_class_get_field_from_name = nullptr;
t_il2cpp_field_get_offset il2cpp_field_get_offset = nullptr;
t_il2cpp_class_get_name il2cpp_class_get_name = nullptr;

// ============================================================
// 2. RESOLVE EXPORTS
// ============================================================
void ResolveIL2CPP() {
    HMODULE lib = GetModuleHandleA("libil2cpp.so");
    if (!lib) {
        // If it's not loaded yet, wait for it
        while (!lib) {
            Sleep(100);
            lib = GetModuleHandleA("libil2cpp.so");
        }
    }

    il2cpp_domain_get = (t_il2cpp_domain_get)GetProcAddress(lib, "il2cpp_domain_get");
    il2cpp_domain_get_assemblies = (t_il2cpp_domain_get_assemblies)GetProcAddress(lib, "il2cpp_domain_get_assemblies");
    il2cpp_assembly_get_image = (t_il2cpp_assembly_get_image)GetProcAddress(lib, "il2cpp_assembly_get_image");
    il2cpp_class_from_name = (t_il2cpp_class_from_name)GetProcAddress(lib, "il2cpp_class_from_name");
    il2cpp_class_get_field_from_name = (t_il2cpp_class_get_field_from_name)GetProcAddress(lib, "il2cpp_class_get_field_from_name");
    il2cpp_field_get_offset = (t_il2cpp_field_get_offset)GetProcAddress(lib, "il2cpp_field_get_offset");
    il2cpp_class_get_name = (t_il2cpp_class_get_name)GetProcAddress(lib, "il2cpp_class_get_name");
}

// ============================================================
// 3. THE SCANNER
// ============================================================
void ScanOffsets() {
    ResolveIL2CPP();

    if (!il2cpp_domain_get) {
        MessageBoxA(nullptr, "Failed to find il2cpp_domain_get!", "Error", MB_ICONERROR);
        return;
    }

    std::ofstream log("C:\\CFL_Offsets.txt");
    log << "--- CFL IL2CPP OFFSET SCANNER ---\n\n";

    void* domain = il2cpp_domain_get();
    size_t assembly_count = 0;
    void** assemblies = il2cpp_domain_get_assemblies(domain, &assembly_count);

    log << "Found " << assembly_count << " assemblies.\n\n";

    for (size_t i = 0; i < assembly_count; i++) {
        void* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) continue;

        // We are looking for the Assembly-CSharp classes
        const char* class_names[] = { "Player", "EntityPlayer", "GamePlayer", "GameObject" };
        
        for (const char* cName : class_names) {
            void* klass = il2cpp_class_from_name(image, "", cName);
            if (!klass) klass = il2cpp_class_from_name(image, "Assembly-CSharp", cName);
            
            if (klass) {
                log << "=== FOUND CLASS: " << cName << " ===\n";
                
                // Look for Transform
                void* field_transform = il2cpp_class_get_field_from_name(klass, "m_Transform");
                if (!field_transform) field_transform = il2cpp_class_get_field_from_name(klass, "transform");
                if (!field_transform) field_transform = il2cpp_class_get_field_from_name(klass, "m_CachedTransform");
                
                if (field_transform) {
                    log << "  -> Transform Offset: 0x" << std::hex << il2cpp_field_get_offset(field_transform) << "\n";
                }

                // Look for Health
                void* field_hp = il2cpp_class_get_field_from_name(klass, "m_HP");
                if (!field_hp) field_hp = il2cpp_class_get_field_from_name(klass, "hp");
                if (!field_hp) field_hp = il2cpp_class_get_field_from_name(klass, "Health");
                
                if (field_hp) {
                    log << "  -> Health Offset: 0x" << std::hex << il2cpp_field_get_offset(field_hp) << "\n";
                }

                // Look for Team
                void* field_team = il2cpp_class_get_field_from_name(klass, "m_Team");
                if (!field_team) field_team = il2cpp_class_get_field_from_name(klass, "team");
                
                if (field_team) {
                    log << "  -> Team Offset: 0x" << std::hex << il2cpp_field_get_offset(field_team) << "\n";
                }
            }
        }
    }

    log << "\n--- SCAN COMPLETE ---\n";
    log.close();

    MessageBoxA(nullptr, "Scan complete! Check C:\\CFL_Offsets.txt", "Success", MB_ICONINFORMATION);
}

// ============================================================
// 4. DLL ENTRY POINT
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // Run the scanner in a new thread so it doesn't freeze the game
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)ScanOffsets, nullptr, 0, nullptr);
    }
    return TRUE;
}
