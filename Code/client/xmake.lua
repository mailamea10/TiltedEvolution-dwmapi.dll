
local function build_client(name, extra_defines)
target(name)
    set_kind("static")
    set_group("Client")
    if extra_defines then
        for _, d in ipairs(extra_defines) do
            add_defines(d)
        end
    end
    add_includedirs(".","../../Libraries/")
    set_pcxxheader("TiltedOnlinePCH.h")

    -- latest game version by default (see main.cpp in client)
    if is_config("skyrim_version", "1.6.1170") then
        add_defines("TP_SKYRIM_1_6_1170", {public = true})
    end

    -- exclude game specifc stuff
    add_headerfiles("**.h|Games/Skyrim/**")
    add_files("**.cpp|Games/Skyrim/**")

    after_install(function(target)
        -- copy dlls
        for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
            local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
            local bindir = path.join(linkdir, "..", "bin")
            os.cp(bindir, target:installdir())
        end
        -- copy ui
        local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
        os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
        os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
        os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
    end)

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_deps(
        "UiProcess",
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "TiltedConnect",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        {inherit = true}
    )

    add_packages(
        "tiltedcore",
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "gamenetworkingsockets",
        "discord",
        "imgui",
        "cef",
        "minhook",
        "entt",
        "glm",
        "mem",
        "xbyak")

    add_syslinks(
        "version",
        "dbghelp",
        "kernel32")
end

build_client("SkyrimTogetherClient")
-- per-version client library for legacy game versions (1.5.x): compiles the
-- engine structs with the 1.5.x layouts (see the SKYRIM_TARGET_LEGACY
-- conditionals in Games/Skyrim). The matching runtime DLL is built by
-- Code/skse_client and loaded by the SKSE bootstrap (Code/skse_bootstrap).
build_client("SkyrimTogetherClientLegacy", {"SKYRIM_TARGET_LEGACY=1"})
