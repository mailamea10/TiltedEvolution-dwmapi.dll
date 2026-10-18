// RaceMenu Together - shares RaceMenu appearances between Skyrim Together players.
//
// This plugin never touches game memory. It tells the RaceMenu Together add-on in the Skyrim Together launcher
// (STR RaceMenu API) that RaceMenu Together is installed and which body morph keys belong to other mods, asks
// RaceMenu for its interface map through SKSE's messaging (the way RaceMenu offers it to other plugins) to
// report which RaceMenu answered, and logs whether the add-on is present. The add-on does the exporting,
// relaying and applying, so this one DLL works on every runtime SKSE supports (1.5.97, 1.6.x and 1.7.x).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../StrAddon/Code/client/RaceMenuTogether/STRRaceMenuAPI.h"

#define RMT_VERSION_MAJOR 1
#define RMT_VERSION_MINOR 0
#define RMT_VERSION_PATCH 0
#define RMT_VERSION_STRING "1.0.0"

namespace
{
// Body morph keys BodyGen (RaceMenu's own NPC body randomizer) and OBody / OBody NG put on NPCs.
const char* const kProtectedMorphKeys[] = {"RSMBodyGen", "OBody", "OBodyNG"};

uint32_t s_raceMenuInterfaceVersion = 0;

uint32_t GetRaceMenuInterfaceVersion()
{
    return s_raceMenuInterfaceVersion;
}

const STRRaceMenu::Descriptor kDescriptor = {
    STRRaceMenu::kApiVersion,
    "RaceMenu Together",
    RMT_VERSION_STRING,
    kProtectedMorphKeys,
    static_cast<uint32_t>(sizeof(kProtectedMorphKeys) / sizeof(kProtectedMorphKeys[0])),
    &GetRaceMenuInterfaceVersion,
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
        s_path = folder + L"\\RaceMenuTogether.log";
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
    snprintf(line, sizeof(line), "RaceMenu Together %s loaded on runtime %u.%u.%u", RMT_VERSION_STRING, (aRuntimeVersion >> 24) & 0xFF, (aRuntimeVersion >> 16) & 0xFF,
             (aRuntimeVersion >> 4) & 0xFFF);
    Log(line);

    // The Skyrim Together launcher hosts the game and reports the game's image as the main module, so look
    // for the add-on's export in every loaded module rather than only in GetModuleHandle(nullptr).
    STRRaceMenu::THostApiVersion hostVersion = nullptr;
    HMODULE modules[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
    {
        const DWORD cCount = (needed / sizeof(HMODULE)) < 1024 ? needed / sizeof(HMODULE) : 1024;
        for (DWORD i = 0; i < cCount && !hostVersion; ++i)
            hostVersion = reinterpret_cast<STRRaceMenu::THostApiVersion>(GetProcAddress(modules[i], STRRaceMenu::kHostApiVersionExport));
    }
    if (hostVersion)
    {
        snprintf(line, sizeof(line), "Skyrim Together add-on found (RaceMenu API %u); RaceMenu appearances will be shared", hostVersion());
        Log(line);
    }
    else
    {
        Log("Skyrim Together add-on not found: the game was started with a SkyrimTogether.exe without the add-on "
            "(see the README's install steps). RaceMenu works as usual, but appearances are not shared.");
    }
}
} // namespace

// --- STR RaceMenu API ----------------------------------------------------------------------------------------

extern "C" __declspec(dllexport) const STRRaceMenu::Descriptor* STRRaceMenu_GetDescriptor()
{
    return &kDescriptor;
}

// --- SKSE ----------------------------------------------------------------------------------------------------

namespace SKSE
{
using PluginHandle = uint32_t;

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
    void* (*QueryInterface)(uint32_t aId);
    PluginHandle (*GetPluginHandle)();
};

constexpr uint32_t kInterface_Messaging = 5;

struct Message
{
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};

using EventCallback = void (*)(Message* apMessage);

struct MessagingInterface
{
    uint32_t interfaceVersion;
    bool (*RegisterListener)(PluginHandle aListener, const char* acpSender, EventCallback aHandler);
    bool (*Dispatch)(PluginHandle aSender, uint32_t aType, void* apData, uint32_t aDataLength, const char* acpReceiver);
};

constexpr uint32_t kMessage_PostPostLoad = 1;

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

// RaceMenu (SKEE) hands its interface map to any plugin that sends it this message; only the map's lookup and
// each interface's version are used here.
namespace RaceMenu
{
constexpr uint32_t kMessage_ExchangeInterface = 0x9E3779B9;

struct PluginInterface
{
    virtual ~PluginInterface() = default;
    virtual uint32_t GetVersion() = 0;
};

struct InterfaceMap
{
    virtual PluginInterface* QueryInterface(const char* acpName) = 0;
};

struct ExchangeMessage
{
    InterfaceMap* Map = nullptr;
};
} // namespace RaceMenu

namespace
{
SKSE::PluginHandle s_handle = 0;
SKSE::MessagingInterface* s_messaging = nullptr;

void OnSkseMessage(SKSE::Message* apMessage)
{
    if (!apMessage || apMessage->type != SKSE::kMessage_PostPostLoad || !s_messaging)
        return;

    // RaceMenu starts answering at PostLoad; this is right after.
    RaceMenu::ExchangeMessage exchange;
    s_messaging->Dispatch(s_handle, RaceMenu::kMessage_ExchangeInterface, &exchange, sizeof(exchange), "SKEE");
    if (!exchange.Map)
    {
        Log("RaceMenu did not answer (not installed, or a version that does not hand out its interfaces). "
            "If RaceMenu is installed, appearances are still shared: the add-on uses RaceMenu's own preset functions.");
        return;
    }

    if (RaceMenu::PluginInterface* pBodyMorph = exchange.Map->QueryInterface("BodyMorph"))
        s_raceMenuInterfaceVersion = pBodyMorph->GetVersion();

    char line[160];
    snprintf(line, sizeof(line), "RaceMenu found (body morph interface v%u)", s_raceMenuInterfaceVersion);
    Log(line);
}
} // namespace

extern "C"
{
__declspec(dllexport) SKSE::PluginVersionData SKSEPlugin_Version = {
    1,
    (RMT_VERSION_MAJOR << 24) | (RMT_VERSION_MINOR << 16) | (RMT_VERSION_PATCH << 4),
    "RaceMenuTogether",
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
    apInfo->name = "RaceMenuTogether";
    apInfo->version = SKSEPlugin_Version.pluginVersion;
    return apSkse && !apSkse->isEditor;
}

__declspec(dllexport) bool SKSEPlugin_Load(const SKSE::Interface* apSkse)
{
    ReportHost(apSkse ? apSkse->runtimeVersion : 0);

    if (apSkse && apSkse->QueryInterface && apSkse->GetPluginHandle)
    {
        s_handle = apSkse->GetPluginHandle();
        s_messaging = static_cast<SKSE::MessagingInterface*>(apSkse->QueryInterface(SKSE::kInterface_Messaging));
        if (!s_messaging || !s_messaging->RegisterListener(s_handle, "SKSE", &OnSkseMessage))
            Log("SKSE messaging is unavailable; RaceMenu's version is not reported (sharing is not affected)");
    }
    return true;
}
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
