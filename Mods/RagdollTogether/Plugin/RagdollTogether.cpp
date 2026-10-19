// Ragdoll Together - shows every Skyrim Together player the same ragdoll when an actor is knocked down or dies.
//
// This plugin never touches game memory. It switches on the Ragdoll Together add-on in the Skyrim Together launcher
// (STR Ragdoll API) and logs whether the add-on is present. The add-on samples, relays and applies the ragdoll poses,
// so this one DLL works on every runtime SKSE supports (1.5.97, 1.6.x and 1.7.x).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "../StrAddon/Code/client/RagdollTogether/STRRagdollAPI.h"

#define RDT_VERSION_MAJOR 1
#define RDT_VERSION_MINOR 0
#define RDT_VERSION_PATCH 0
#define RDT_VERSION_STRING "1.0.0"

namespace
{
const STRRagdoll::Descriptor kDescriptor = {
    STRRagdoll::kApiVersion,
    "Ragdoll Together",
    RDT_VERSION_STRING,
};

void Log(const char* acpLine)
{
    static std::wstring s_path;
    if (s_path.empty())
    {
        PWSTR pDocuments = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pDocuments)))
            return;
        std::wstring base = std::wstring(pDocuments) + L"\\My Games\\";
        CoTaskMemFree(pDocuments);

        // GOG installs keep their own folder.
        std::wstring folder = base + L"Skyrim Special Edition GOG\\SKSE";
        if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
            folder = base + L"Skyrim Special Edition\\SKSE";
        SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        s_path = folder + L"\\RagdollTogether.log";
        DeleteFileW(s_path.c_str());
    }

    FILE* pFile = nullptr;
    if (_wfopen_s(&pFile, s_path.c_str(), L"a") == 0 && pFile)
    {
        SYSTEMTIME time;
        GetLocalTime(&time);
        fprintf(pFile, "[%02d:%02d:%02d] %s\n", time.wHour, time.wMinute, time.wSecond, acpLine);
        fclose(pFile);
    }
}

void ReportHost(uint32_t aRuntimeVersion)
{
    char line[320];
    snprintf(line, sizeof(line), "Ragdoll Together %s loaded on runtime %u.%u.%u", RDT_VERSION_STRING, (aRuntimeVersion >> 24) & 0xFF, (aRuntimeVersion >> 16) & 0xFF,
             (aRuntimeVersion >> 4) & 0xFFF);
    Log(line);

    // The Skyrim Together launcher hosts the game and reports the game's image as the main module, so look for the
    // add-on's export in every loaded module rather than only in GetModuleHandle(nullptr).
    STRRagdoll::THostApiVersion hostVersion = nullptr;
    HMODULE modules[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
    {
        const DWORD cCount = (needed / sizeof(HMODULE)) < 1024 ? needed / sizeof(HMODULE) : 1024;
        for (DWORD i = 0; i < cCount && !hostVersion; ++i)
            hostVersion = reinterpret_cast<STRRagdoll::THostApiVersion>(GetProcAddress(modules[i], STRRagdoll::kHostApiVersionExport));
    }
    if (hostVersion)
    {
        snprintf(line, sizeof(line), "Skyrim Together add-on found (Ragdoll API %u); ragdolls will be shared", hostVersion());
        Log(line);
    }
    else
    {
        Log("Skyrim Together add-on not found: the game was started with a SkyrimTogether.exe without the add-on "
            "(see the README's install steps). Ragdolls work as usual, but are not shared.");
    }
}
} // namespace

// --- STR Ragdoll API -----------------------------------------------------------------------------------------

extern "C" __declspec(dllexport) const STRRagdoll::Descriptor* STRRagdoll_GetDescriptor()
{
    return &kDescriptor;
}

// --- SKSE ----------------------------------------------------------------------------------------------------

namespace SKSE
{
// SKSE 2.0.x (1.5.97) query interface.
struct PluginInfo
{
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

struct Interface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
};

// SKSE 2.1+ (1.6.x and later) version data.
struct PluginVersionData
{
    uint32_t dataVersion;
    uint32_t pluginVersion;
    char name[256];
    char author[256];
    char supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};
static_assert(sizeof(PluginVersionData) == 0x350);

constexpr uint32_t kVersionIndependent_Signatures = 1 << 1;
constexpr uint32_t kVersionIndependent_StructsPost629 = 1 << 2;
constexpr uint32_t kVersionIndependentEx_NoStructUse = 1 << 0;
} // namespace SKSE

extern "C"
{
__declspec(dllexport) SKSE::PluginVersionData SKSEPlugin_Version = {
    1,
    (RDT_VERSION_MAJOR << 24) | (RDT_VERSION_MINOR << 16) | (RDT_VERSION_PATCH << 4),
    "RagdollTogether",
    "mailamea10",
    "",
    // No game addresses and no game structures are used, so any runtime is fine.
    SKSE::kVersionIndependentEx_NoStructUse,
    SKSE::kVersionIndependent_Signatures | SKSE::kVersionIndependent_StructsPost629,
    {0},
    0,
};

__declspec(dllexport) bool SKSEPlugin_Query(const SKSE::Interface* apSkse, SKSE::PluginInfo* apInfo)
{
    apInfo->infoVersion = 1;
    apInfo->name = "RagdollTogether";
    apInfo->version = SKSEPlugin_Version.pluginVersion;
    return apSkse && !apSkse->isEditor;
}

__declspec(dllexport) bool SKSEPlugin_Load(const SKSE::Interface* apSkse)
{
    ReportHost(apSkse ? apSkse->runtimeVersion : 0);
    return true;
}
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
