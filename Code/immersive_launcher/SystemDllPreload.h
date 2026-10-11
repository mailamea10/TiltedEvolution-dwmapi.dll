#pragma once

// Pre-binds System32 copies of DLLs that could otherwise be shadowed by the
// extra DLL directories STR registers (the Steam folder, see
// steam/SteamLoader.cpp). Windows reuses an already-loaded module for
// same-name imports, so a later import of dwmapi.dll gets this one.
// Call at the very start of launcher::StartUp().
//
// Deliberately NOT listed: d3d11/dxgi/d3d9/dinput8/version, which ENB,
// ReShade etc. intentionally proxy from the game folder.

#include <Windows.h>

namespace launcher
{
inline void PreloadSystemDlls()
{
    static const wchar_t* const kSystemOnly[] = {
        L"dwmapi.dll",
    };

    for (const wchar_t* name : kSystemOnly)
    {
        // Searches System32 only. Never freed, so it stays resident.
        LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
}
} // namespace launcher
