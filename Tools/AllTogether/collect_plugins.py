#!/usr/bin/env python3
"""Takes the SKSE plugins out of the add-ons' release archives, for assemble_fomod.py.

Usage: collect_plugins.py <archives> <out>

<archives> holds one folder per add-on (ragdoll, lipsync, racemenu, skyparkour), each with that add-on's release
archive. <out> gets <Name>/SKSE/Plugins (the plugin DLL, and LipSync's default INI), <Name>/Docs, and LipSync's
relay choices in LipSync/security.
"""
import shutil
import sys
from pathlib import Path
from zipfile import ZipFile

ADDONS = {"Ragdoll": "ragdoll", "LipSync": "lipsync", "RaceMenu": "racemenu", "SkyParkour": "skyparkour"}


def fail(message: str) -> None:
    sys.exit(f"collect_plugins: {message}")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    archives, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    if out.exists():
        shutil.rmtree(out)

    for addon, folder in ADDONS.items():
        found = list((archives / folder).glob("*.zip"))
        if len(found) != 1:
            fail(f"expected one {addon} archive in {archives / folder}, found {len(found)}")
        with ZipFile(found[0]) as archive:
            names = [n.replace("\\", "/") for n in archive.namelist()]
            dlls = [n for n in names if n.startswith("00 Core/SKSE/Plugins/") and n.lower().endswith(".dll")]
            if len(dlls) != 1:
                fail(f"expected one plugin DLL in {found[0].name}, found {dlls}")
            wanted = dlls + [n for n in names if n.lower() == "00 core/skse/plugins/lipsynctogether.ini"]
            for name in wanted:
                dest = out / addon / "SKSE" / "Plugins" / Path(name).name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(archive.read(name))
            for name in names:
                # The add-on archives' launcher manifests describe their own launchers, not the bundle's.
                if name.startswith("00 Core/Docs/") and not name.endswith("/") and "manifest" not in name.lower():
                    dest = out / addon / "Docs" / Path(name).name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(archive.read(name))
            if addon == "LipSync":
                for ini in ("LipSyncTogether-Secure.ini", "LipSyncTogether-Open.ini"):
                    member = next((n for n in names if n.lower() == f"fomod/security/{ini}".lower()), None)
                    if not member:
                        fail(f"{found[0].name} has no fomod/security/{ini}")
                    dest = out / "LipSync" / "security" / ini
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(archive.read(member))
        print(f"collect_plugins: {addon} from {found[0].name}")


if __name__ == "__main__":
    main()
