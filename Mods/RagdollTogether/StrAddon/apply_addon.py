"""Adds the Ragdoll Together add-on to a Skyrim Together Reborn source tree.

Usage: python apply_addon.py <path to the STR source tree> [--protocol 1.8.3|1.8.4|1.8.5|1.8.7]

Copies the add-on's client sources into Code/client and registers RagdollTogetherService in Code/client/World.cpp.
Poses travel as Plugin API V2 messages about the ragdolled actor, so the tree must carry Plugin API V2 (Skyrim
Together builds with it, or the Together Launcher host patch on older ones); the script stops if it does not. Players
on a server without V2 simply do not share ragdolls.

1.8.3 and 1.8.4 have no playback clock for remote movement, which --protocol accounts for (default 1.8.5).
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

    if protocol in ("1.8.3", "1.8.4"):
        service = tree / "Code" / "client" / "Services" / "Generic" / "RagdollTogetherService.cpp"
        text = service.read_text(encoding="utf-8")
        # 1.8.3 and 1.8.4: no playback clock (see STR_RAGDOLL_PROTOCOL_183).
        legacy = "#define STR_RAGDOLL_PROTOCOL_183 0"
        if legacy not in text:
            sys.exit(f"apply_addon: '{legacy}' not found in {service}")
        service.write_text(text.replace(legacy, "#define STR_RAGDOLL_PROTOCOL_183 1"), encoding="utf-8")
        print(f"apply_addon: protocol {protocol}")

    world = tree / "Code" / "client" / "World.cpp"
    text = world.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Services/MapService.h>", "#include <Services/RagdollTogetherService.h>\n", world)
    text = insert_after(text, "ctx().emplace<MapService>(", "    ctx().emplace<RagdollTogetherService>(*this, m_transport, m_dispatcher);\n", world)
    world.write_text(text, encoding="utf-8")
    print(f"apply_addon: registered RagdollTogetherService in {world.relative_to(tree)}")


if __name__ == "__main__":
    main()
