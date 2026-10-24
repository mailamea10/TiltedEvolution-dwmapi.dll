
#include <windows.h>
#include <OverlayProc.hpp>
#include "ProcessHandler.h"

namespace
{
// TPProcess.exe lives in the game folder, so Windows searches that folder first
// when this helper (or Chromium inside it) loads a DLL by name. Mod loaders that
// proxy system DLLs there (ENB's d3d11.dll / dxgi.dll / d3d9.dll, a dwmapi.dll
// proxy, ...) therefore get pulled into every CEF helper, where their SKSE/KiLoader
// payload starts up inside a process that is not Skyrim and fails with a popup.
//
// Loading the genuine System32 copy by full path first registers the module under
// its base name, so any later by-name load resolves to it instead of the proxy.
void PinSystemLibraries() noexcept
{
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    static constexpr const wchar_t* kNames[] = {L"d3d11.dll", L"dxgi.dll", L"d3d9.dll", L"d3d10.dll", L"d3d12.dll",
                                                L"dwmapi.dll", L"dinput8.dll", L"winmm.dll",  L"version.dll"};

    for (const wchar_t* name : kNames)
    {
        wchar_t path[MAX_PATH]{};
        if (swprintf_s(path, L"%s\\%s", systemDir, name) > 0)
            LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
}
} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    PinSystemLibraries();

    TiltedPhoques::UIMain(lpCmdLine, hInstance, []() { return new ProcessHandler; });
}
