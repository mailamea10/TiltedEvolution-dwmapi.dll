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
for image in (root / "Tools/AllTogether/fomod/images").iterdir():
    if image.is_file():
        shutil.copy2(image, out / "fomod/images" / image.name)

archives = {
    "Ragdoll": "ragdoll",
    "LipSync": "lipsync",
    "RaceMenu": "racemenu",
    "SkyParkour": "skyparkour",
}
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

        # Preserve each add-on's own installation notes alongside the bundle guide.
        for name in archive.namelist():
            normalized = name.replace("\\", "/")
            if normalized.startswith("00 Core/Docs/") and not normalized.endswith("/"):
                doc_name = Path(normalized).name
                (out / "Docs" / f"{addon} - {doc_name}").write_bytes(archive.read(name))

        if addon == "LipSync":
            ini_name = next((n for n in archive.namelist() if n.replace("\\", "/").lower() == "00 core/skse/plugins/lipsynctogether.ini"), None)
            if not ini_name:
                raise SystemExit("LipSync archive is missing its default LipSyncTogether.ini")
            target_ini = out / "Add-ons/LipSync/SKSE/Plugins/LipSyncTogether.ini"
            target_ini.write_bytes(archive.read(ini_name))
            for security_name in ("LipSyncTogether-Secure.ini", "LipSyncTogether-Open.ini"):
                member = next((n for n in archive.namelist() if n.replace("\\", "/").lower() == f"fomod/security/{security_name}".lower()), None)
                if not member:
                    raise SystemExit(f"LipSync archive is missing FOMOD option {security_name}")
                target_security = out / "fomod/security" / security_name
                target_security.parent.mkdir(parents=True, exist_ok=True)
                target_security.write_bytes(archive.read(member))

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

# The published add-on archives contain older 1.5.97 launcher overlays. Replace
# that selection with the bundle's own STR 1.8.5 legacy launcher, built from the
# KiLoader-support commit so the selected compatibility option cannot silently
# fall back to a stale launcher from whichever add-on archive was processed first.
legacy_source = root / "bundle-output/legacy-ki-loader/SkyrimTogetherReborn"
legacy_dest = out / "Launchers/185-1.5.97/SkyrimTogetherReborn"
required_legacy_files = (
    "SkyrimTogether.exe",
    "EarlyLoad.dll",
    "TPProcess.exe",
    "versionlib-ae-to-se-1-5-97-0.map",
)
missing_legacy_files = [name for name in required_legacy_files if not (legacy_source / name).is_file()]
if missing_legacy_files:
    raise SystemExit(f"KiLoader-patched STR 1.8.5 / Skyrim 1.5.97 launcher is missing: {missing_legacy_files}")
legacy_dest.mkdir(parents=True, exist_ok=True)
for filename in required_legacy_files:
    shutil.copy2(legacy_source / filename, legacy_dest / filename)

print(f"Assembled FOMOD at {out}")
