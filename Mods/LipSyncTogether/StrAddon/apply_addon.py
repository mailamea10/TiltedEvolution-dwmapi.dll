"""Adds the LipSync Together add-on to a Skyrim Together Reborn source tree.

Usage: python apply_addon.py <path to the STR source tree> [--protocol 1.8.3|1.8.4|1.8.5|1.8.7]

Copies the add-on's client sources into Code/client, registers LipSyncTogetherService in
Code/client/World.cpp, has the papyrus registration hook in Code/client/Games/Misc/BSScript.cpp hand each
native to the add-on (it needs Mfg Fix's function object, not only the address the papyrus service records), and
has the connect handler in Code/client/Services/Generic/OverlayClient.cpp report the server it dials (voice chat
reaches its relay on that host), and has Code/client/Services/Generic/ImguiService.cpp give the settings window a
moment between frames to load its fonts.
The tree must carry Plugin API V2 (Skyrim Together builds with it, or the Together Launcher host patch on older
ones): mouth states and voice keys go through it. Without V2 on the server they fall back to the script animation
relay on 1.8.3 to 1.8.5, which address relayed script animations differently (--protocol, default 1.8.5), and to
party Mod Messages on 1.8.7 (--protocol 1.8.7), whose servers refuse relayed add-on names.
"""

import pathlib
import shutil
import sys


def insert_after(text: str, anchor: str, addition: str, path: pathlib.Path) -> str:
    if addition.strip() in text:
        return text
    index = text.find(anchor)
    if index < 0:
        sys.exit(f"apply_addon: '{anchor.strip()}' not found in {path}")
    end = text.index("\n", index) + 1
    return text[:end] + addition + text[end:]


def insert_before(text: str, anchor: str, addition: str, path: pathlib.Path) -> str:
    if addition.strip() in text:
        return text
    index = text.find(anchor)
    if index < 0:
        sys.exit(f"apply_addon: '{anchor.strip()}' not found in {path}")
    start = text.rindex("\n", 0, index) + 1
    return text[:start] + addition + text[start:]


def main() -> None:
    args = sys.argv[1:]
    protocol = "1.8.5"
    if "--protocol" in args:
        index = args.index("--protocol")
        protocol = args[index + 1] if index + 1 < len(args) else ""
        del args[index : index + 2]
    if len(args) != 1 or protocol not in ("1.8.3", "1.8.4", "1.8.5", "1.8.7"):
        sys.exit(__doc__)

    tree = pathlib.Path(args[0]).resolve()
    addon = pathlib.Path(__file__).resolve().parent

    api = tree / "Code" / "plugin_api" / "include" / "SkyrimTogetherAPI.h"
    if not api.is_file() or "IVSkyrimTogether2" not in api.read_text(encoding="utf-8"):
        sys.exit(f"apply_addon: {tree} has no Plugin API V2; apply the Together Launcher host patch first")

    source_root = addon / "Code"
    for source in source_root.rglob("*"):
        if source.is_file():
            target = tree / "Code" / source.relative_to(source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            print(f"apply_addon: copied {target.relative_to(tree)}")

    if protocol == "1.8.7":
        service = tree / "Code" / "client" / "Services" / "Generic" / "LipSyncTogetherService.cpp"
        text = service.read_text(encoding="utf-8")
        default = "#define STR_LIPSYNC_HOST_PARTY_API 0"
        if default not in text:
            sys.exit(f"apply_addon: '{default}' not found in {service}")
        # 1.8.7: the server refuses relayed STR*| names but has party Mod Messages.
        service.write_text(text.replace(default, "#define STR_LIPSYNC_HOST_PARTY_API 1"), encoding="utf-8")
        print("apply_addon: protocol 1.8.7")

    if protocol == "1.8.3":
        service = tree / "Code" / "client" / "Services" / "Generic" / "LipSyncTogetherService.cpp"
        text = service.read_text(encoding="utf-8")
        default = "#define STR_LIPSYNC_NO_OBJECT 0xFFFFFFFFu"
        if default not in text:
            sys.exit(f"apply_addon: '{default}' not found in {service}")
        # 1.8.3: the stock client skips script animations for object id 0.
        service.write_text(text.replace(default, "#define STR_LIPSYNC_NO_OBJECT 0u"), encoding="utf-8")
        print("apply_addon: protocol 1.8.3")

    world = tree / "Code" / "client" / "World.cpp"
    text = world.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Services/MapService.h>", "#include <Services/LipSyncTogetherService.h>\n", world)
    text = insert_after(text, "ctx().emplace<MapService>(", "    ctx().emplace<LipSyncTogetherService>(*this, m_transport, m_dispatcher);\n", world)
    world.write_text(text, encoding="utf-8")
    print(f"apply_addon: registered LipSyncTogetherService in {world.relative_to(tree)}")

    script = tree / "Code" / "client" / "Games" / "Misc" / "BSScript.cpp"
    text = script.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Misc/NativeFunction.h>", "#include <LipSyncTogether/LipSyncNatives.h>\n", script)
    text = insert_before(text, "TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);",
                         "    LipSyncTogether_OnNativeRegistered(apFunction);\n", script)
    script.write_text(text, encoding="utf-8")
    print(f"apply_addon: papyrus registration hook in {script.relative_to(tree)} reports natives to LipSync Together")

    overlay = tree / "Code" / "client" / "Services" / "Generic" / "OverlayClient.cpp"
    text = overlay.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Services/OverlayClient.h>", "#include <LipSyncTogether/LipSyncNatives.h>\n", overlay)
    text = insert_after(text, 'std::string endpoint = baseIp + ":" + std::to_string(port);',
                        "    LipSyncTogether_OnConnect(endpoint.c_str());\n", overlay)
    overlay.write_text(text, encoding="utf-8")
    print(f"apply_addon: the connect handler in {overlay.relative_to(tree)} tells LipSync Together which server it dials")

    imgui = tree / "Code" / "client" / "Services" / "Generic" / "ImguiService.cpp"
    text = imgui.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Services/ImguiService.h>", "#include <LipSyncTogether/LipSyncNatives.h>\n", imgui)
    text = insert_before(text, "    ImGui_ImplDX11_NewFrame();", "    LipSyncTogether_BeforeImGuiFrame();\n", imgui)
    imgui.write_text(text, encoding="utf-8")
    print(f"apply_addon: {imgui.relative_to(tree)} lets LipSync Together add its fonts between ImGui frames")


if __name__ == "__main__":
    main()
