local function build_launcher()
    set_kind("binary")
    set_group("Client")
    set_symbols("debug", "hidden")

    add_ldflags(
        "/FORCE:MULTIPLE",
        "/IGNORE:4254,4006",
        "/DYNAMICBASE:NO",
        "/SAFESEH:NO",
        "/LARGEADDRESSAWARE",
        "/INCREMENTAL:NO",
        "/LAST:.zdata",
        "/SUBSYSTEM:WINDOWS",
        "/ENTRY:mainCRTStartup", { force = true })
    add_includedirs(
        ".",
        "../",
        "../../Libraries/")
    add_headerfiles("**.h")
    add_files(
        "**.cpp",
        "launcher.rc")
    add_deps(
        "ImmersiveElf",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        "ImGuiImpl",
        "CommonLib")
    add_links("ntdll_x64")
    add_linkdirs(".")
    add_syslinks(
        "user32",
        "shell32",
        "comdlg32",
        "bcrypt",
        "ole32",
        "dxgi",
        "d3d11",
        "gdi32",
        "SetupAPI",
        "Powrprof",
        "Cfgmgr32",
        "Propsys",
        "delayimp")

    add_packages(
        "tiltedcore",
        "spdlog",
        "minhook",
        "hopscotch-map",
        "cryptopp",
        "glm",
        "cef",
        "mem")
end

target("SkyrimImmersiveLauncher")
    set_basename("SkyrimTogether")
    add_defines("TARGET_PREFIX=\"st\"")
    add_deps("SkyrimTogetherClient")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherClient", { force = true })
    build_launcher()

-- Skyrim SE 1.5.x (pre-AE) build of the same launcher. It links the legacy
-- client (SKYRIM_TARGET_LEGACY) and keeps the release file name, so it goes to
-- its own folder instead of overwriting the 1.6 SkyrimTogether.exe. The 1.5.97
-- executable is SteamStub-wrapped; ExeLoader::DecryptCeg already handles that.
target("SkyrimImmersiveLauncherLegacy")
    set_basename("SkyrimTogether")
    set_targetdir("$(buildir)/$(plat)/$(arch)/$(mode)/legacy")
    add_defines("TARGET_PREFIX=\"st\"", "SKYRIM_TARGET_LEGACY=1")
    add_deps("SkyrimTogetherClientLegacy")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherClientLegacy", { force = true })
    build_launcher()

-- The same 1.5.x launcher without its console window, for players who do not
-- want one. Both variants ship in a release.
target("SkyrimImmersiveLauncherLegacyNoConsole")
    set_basename("SkyrimTogether")
    set_targetdir("$(buildir)/$(plat)/$(arch)/$(mode)/legacy-noconsole")
    add_defines("TARGET_PREFIX=\"st\"", "SKYRIM_TARGET_LEGACY=1", "STR_PUBLIC_RELEASE=1")
    add_deps("SkyrimTogetherClientLegacy")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherClientLegacy", { force = true })
    build_launcher()
