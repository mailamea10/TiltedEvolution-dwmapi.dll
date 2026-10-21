#!/usr/bin/env python3
"""Assemble the published add-on archives and launchers into one FOMOD."""
from pathlib import Path
from zipfile import ZipFile
import shutil
import sys


root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
if out.exists():
    shutil.rmtree(out)
(out / "fomod" / "images").mkdir(parents=True)
(out / "Docs").mkdir()
(out / "Add-ons").mkdir()
(out / "Launchers").mkdir()
shutil.copy2(root / "Tools/AllTogether/fomod/info.xml", out / "fomod/info.xml")
shutil.copy2(root / "Tools/AllTogether/fomod/ModuleConfig.xml", out / "fomod/ModuleConfig.xml")
shutil.copy2(root / "Tools/AllTogether/INSTALLATION.txt", out / "Docs/AllTogether SKSE - Installation Instructions.txt")

archives = {
    "Ragdoll": "ragdoll",
    "LipSync": "lipsync",
    "RaceMenu": "racemenu",
    "SkyParkour": "skyparkour",
}
banner_written = False
for addon, source_dir in archives.items():
    files = list((root / "bundle-output/addons" / source_dir).glob("*.zip"))
    if len(files) != 1:
        raise SystemExit(f"Expected one latest {addon} archive; found {len(files)}")
    with ZipFile(files[0]) as archive:
        dlls = [n for n in archive.namelist() if n.startswith("00 Core/SKSE/Plugins/") and n.lower().endswith(".dll")]
        if len(dlls) != 1:
            raise SystemExit(f"Expected one {addon} plugin DLL, found {dlls}")
        target = out / "Add-ons" / addon / "SKSE/Plugins" / Path(dlls[0]).name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive.read(dlls[0]))
        if not banner_written:
            name = next((n for n in archive.namelist() if n.lower() == "fomod/images/banner.png"), None)
            if not name:
                raise SystemExit("The source package does not contain the standard FOMOD banner")
            (out / "fomod/images/banner.png").write_bytes(archive.read(name))
            banner_written = True

        for code, source in [("183-1.7.104", "10 STR 1.8.3 - 1.7.104"), ("183-1.6.1170", "11 STR 1.8.3 - 1.6.1170"),
                             ("185-1.7.104", "20 STR 1.8.5 - 1.7.104"), ("185-1.6.1170", "21 STR 1.8.5 - 1.6.1170"),
                             ("185-1.5.97", "22 STR 1.8.5 - 1.5.97"), ("186-1.7.104", "30 STR 1.8.6 - 1.7.104"),
                             ("186-1.6.1170", "31 STR 1.8.6 - 1.6.1170")]:
            dest = out / "Launchers" / code
            if dest.exists():
                continue
            prefix = source + "/"
            entries = [n for n in archive.namelist() if n.startswith(prefix) and not n.endswith("/")]
            if not entries:
                raise SystemExit(f"Missing legacy launcher folder {source}")
            for name in entries:
                relative = name[len(prefix):]
                target = dest / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))

for game, label in [("1.7.104", "187-1.7.104"), ("1.6.1170", "187-1.6.1170")]:
    matches = list(root.glob(f"bundle-output/**/STR-1.8.7-Skyrim-{game}/SkyrimTogether.exe"))
    if len(matches) != 1:
        raise SystemExit(f"Expected one STR 1.8.7 launcher for Skyrim {game}, found {len(matches)}")
    source_dir = matches[0].parent
    dest = out / "Launchers" / label / "SkyrimTogetherReborn"
    dest.mkdir(parents=True)
    for filename in ("SkyrimTogether.exe", "EarlyLoad.dll"):
        source = source_dir / filename
        if source.exists():
            shutil.copy2(source, dest / filename)

print(f"Assembled FOMOD at {out}")
