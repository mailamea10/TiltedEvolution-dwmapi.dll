"""Adds the STR Mod Sync add-on to a Skyrim Together Reborn 1.8.5 source tree.

Usage: python apply_addon.py <path to the STR source tree>

Copies the add-on's client sources into Code/client and registers ModSyncService in Code/client/World.cpp.
The same add-on goes onto the 1.5.97, 1.6.1170/1.6.1179 and 1.7.104 trees, which all share 1.8.5's client.
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
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    tree = pathlib.Path(sys.argv[1]).resolve()
    addon = pathlib.Path(__file__).resolve().parent

    source_root = addon / "Code"
    for source in source_root.rglob("*"):
        if source.is_file():
            target = tree / "Code" / source.relative_to(source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            print(f"apply_addon: copied {target.relative_to(tree)}")

    world = tree / "Code" / "client" / "World.cpp"
    text = world.read_text(encoding="utf-8")
    text = insert_after(text, "#include <Services/MapService.h>", "#include <Services/ModSyncService.h>\n", world)
    text = insert_after(text, "ctx().emplace<MapService>(", "    ctx().emplace<ModSyncService>(*this, m_transport, m_dispatcher);\n", world)
    world.write_text(text, encoding="utf-8")
    print(f"apply_addon: registered ModSyncService in {world.relative_to(tree)}")


if __name__ == "__main__":
    main()
