
target("TPProcess")
    set_kind("binary")
    set_group("Client")

    if is_plat("windows") then
        add_ldflags("/subsystem:windows")
        -- libcef.dll is delay-loaded so WinMain can pin the genuine system DLLs
        -- (see PinSystemLibraries) before Chromium pulls in anything by name
        add_ldflags("/DELAYLOAD:libcef.dll")
        add_syslinks("delayimp")
    end

    add_includedirs(
        ".")
    add_headerfiles("*.h")
    add_files(
        "*.cpp",
        "process.rc")
    add_deps("UiProcess")
    add_packages("tiltedcore", "cef", "hopscotch-map")