
#include <TiltedOnlineApp.h>
#include <TiltedOnlinePCH.h>
#include <ScriptExtender.h>

#include <Commctrl.h>
#include <Windows.h>

#include <base/dialogues/win/TaskDialog.h>

#include <GameRoot.h>

#ifndef SKYRIM_TARGET_LEGACY
// 1 - Steam, 2 - GOG
inline constexpr std::string_view kSupportedGameVersions[2] = {"1.6.1170.0", "1.6.1179.0"};
#endif

std::unique_ptr<TiltedOnlineApp> g_appInstance{nullptr};

extern HICON g_SharedWindowIcon;

namespace
{
std::filesystem::path s_gameRoot;
} // namespace

const std::filesystem::path& GetGameRoot()
{
    return s_gameRoot;
}

static void ShowAddressLibraryError(const wchar_t* apGamePath)
{
    auto errorDetail = fmt::format(L"Looking for it here: {}\\Data\\SKSE\\Plugins", apGamePath);

#ifdef SKYRIM_TARGET_LEGACY
    const wchar_t* mainText = L"Game version 1.5.x requires version-<version>.bin (from \"All in one (Special Edition)\") "
                              L"and the AE-to-SE id mapping file versionlib-ae-to-se-<version>.map";
#else
    const wchar_t* mainText = L"Make sure to use \"All in one (1.6.X)\"";
#endif

    Base::TaskDialog dia(g_SharedWindowIcon, L"Error", L"Failed to load Skyrim Address Library", mainText, errorDetail.c_str());

    dia.AppendButton(0xBEED, L"Visit troubleshooting page on wiki.tiltedphoques.com");
    dia.AppendButton(0xBEEF, L"Visit Address Library modpage on nexusmods.com");
    const int result = dia.Show();
    if (result == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (result == 0xBEED)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://wiki.tiltedphoques.com/tilted-online/guides/troubleshooting/address-library-error)", nullptr, nullptr, SW_SHOWNORMAL);
    }

    exit(4);
}

static void ShowIncompatibleVersionError(const char* apDetectedGameVersion, const wchar_t* apGamePath)
{
    constexpr wchar_t kModPageUrl[] = LR"(https://www.nexusmods.com/skyrimspecialedition/mods/69993?tab=files)";
#ifdef SKYRIM_TARGET_LEGACY
    std::string supportedVersions = "1.5.x";
#else
    const auto [steamVer, gogVer] = kSupportedGameVersions;

    std::string supportedVersions = steamVer != gogVer ? fmt::format("{} (or {} if GOG)", steamVer, gogVer) : std::string{steamVer};
#endif
    std::string message = fmt::format("Skyrim Together {} requires Skyrim SE {}, but your installed version is {}\n\nUpdate or downgrade to match, then relaunch", BUILD_COMMIT + 1, supportedVersions, apDetectedGameVersion);
    std::wstring wideMessage(message.begin(), message.end());

    const auto optionalDetails = fmt::format(L"Installed here: {}", apGamePath);

    Base::TaskDialog dia(g_SharedWindowIcon, L"Error", L"Incompatible game version", wideMessage.c_str(), optionalDetails.c_str());
    dia.AppendButton(0xBEEF, L"Visit Skyrim Together mod page on nexusmods.com");

    if (dia.Show() == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", kModPageUrl, nullptr, nullptr, SW_SHOWNORMAL);
    }
    exit(4);
}

void RunTiltedInit(const std::filesystem::path& acGamePath, const String& aExeVersion)
{
    s_gameRoot = acGamePath;

    if (!VersionDb::Get().Load(acGamePath, aExeVersion))
    {
        ShowAddressLibraryError(acGamePath.c_str());
    }

#ifdef SKYRIM_TARGET_LEGACY
    // Legacy runtime: any 1.5.x game, as long as the address library for that
    // exact version (and its AE-to-SE id map) loaded above.
    const bool bSupportedVersion = aExeVersion.rfind("1.5.", 0) == 0;
#else
    auto [steamVer, gogVer] = kSupportedGameVersions;
    const bool bSupportedVersion = aExeVersion == steamVer || aExeVersion == gogVer;
#endif
    if (!bSupportedVersion)
    {
        ShowIncompatibleVersionError(aExeVersion.c_str(), acGamePath.c_str());
    }

    g_appInstance = std::make_unique<TiltedOnlineApp>();

#ifdef SKYRIM_TARGET_LEGACY
    spdlog::info("address library loaded: game {}, {} ids, {}", VersionDb::Get().GetLoadedVersionString(),
                 VersionDb::Get().GetOffsetMap().size(),
                 VersionDb::Get().IsLegacyFormat() ? "pre-AE library + AE id map" : "AE library");
    spdlog::warn("legacy game version detected (1.5.x): unmapped ids degrade to stubs, "
                 "1.6.x-only patches are skipped, struct offsets may differ.");
#endif

    TiltedOnlineApp::InstallHooks2();
    TP_HOOK_COMMIT;

    LoadScriptExtender();
}

void RunTiltedApp()
{
    g_appInstance->BeginMain();
}
