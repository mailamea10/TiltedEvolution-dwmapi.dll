#!/usr/bin/env python3
"""Assembles the AllTogether FOMOD from built launchers and the four add-on plugins.

Usage: assemble_fomod.py <launchers> <plugins> <out>

<launchers> is what Mods/TogetherLauncher/build_launcher.py collected (on release/together-launcher): one
"<NN> STR <version> - <runtime>/SkyrimTogetherReborn" folder per launcher, "server-<version>/SkyrimTogetherReborn"
per Skyrim Together release, and LAUNCHER_MANIFEST.txt. <plugins> holds, per add-on, <Name>/SKSE/Plugins/<dll>
(Ragdoll, LipSync, RaceMenu, SkyParkour), the add-on's Docs in <Name>/Docs, and LipSync's
LipSyncTogether-Secure.ini and LipSyncTogether-Open.ini in LipSync/security.

Every launcher and server that fomod/ModuleConfig.xml names must be present, and nothing it does not name is
packed, so the installer cannot offer a choice without files.
"""
import re
import shutil
import sys
from pathlib import Path

ADDONS = ["Ragdoll", "LipSync", "RaceMenu", "SkyParkour"]


def fail(message: str) -> None:
    sys.exit(f"assemble_fomod: {message}")


def release_code(version: str) -> str:
    # "1.8.7-4" -> "187", "v1.8.3" -> "183"
    match = re.match(r"v?(\d)\.(\d)\.(\d)", version)
    if not match:
        fail(f"cannot read a Skyrim Together version from '{version}'")
    return "".join(match.groups())


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    launchers, plugins, out = (Path(a).resolve() for a in sys.argv[1:])
    here = Path(__file__).resolve().parent
    if out.exists():
        shutil.rmtree(out)

    # The installer's own files.
    (out / "fomod" / "images").mkdir(parents=True)
    for name in ("info.xml", "ModuleConfig.xml"):
        shutil.copy2(here / "fomod" / name, out / "fomod" / name)
    for image in (here / "fomod" / "images").iterdir():
        shutil.copy2(image, out / "fomod" / "images" / image.name)
    docs = out / "Docs"
    docs.mkdir()
    shutil.copy2(here / "INSTALLATION.txt", docs / "AllTogether SKSE - Installation Instructions.txt")

    # Launchers: "41 STR 1.8.7-4 - 1.6.1170" -> Launchers/187-1.6.1170; servers: "server-v1.8.3" -> Servers/183.
    for folder in sorted(p for p in launchers.iterdir() if p.is_dir()):
        match = re.fullmatch(r"\d+ STR (\S+) - (\S+)", folder.name)
        if match:
            dest = out / "Launchers" / f"{release_code(match.group(1))}-{match.group(2)}"
        elif folder.name.startswith("server-"):
            dest = out / "Servers" / release_code(folder.name[len("server-"):])
        else:
            continue
        shutil.copytree(folder, dest)
    shutil.copy2(launchers / "LAUNCHER_MANIFEST.txt", docs / "AllTogether SKSE - Launcher manifest.txt")

    # Add-on plugins, docs and LipSync's relay settings.
    for addon in ADDONS:
        source = plugins / addon
        dlls = list((source / "SKSE" / "Plugins").glob("*.dll"))
        if len(dlls) != 1:
            fail(f"expected one {addon} plugin DLL in {source / 'SKSE' / 'Plugins'}, found {len(dlls)}")
        shutil.copytree(source / "SKSE", out / "Add-ons" / addon / "SKSE")
        for doc in sorted((source / "Docs").glob("*")) if (source / "Docs").is_dir() else []:
            shutil.copy2(doc, docs / f"{addon} - {doc.name}")
    for name in ("LipSyncTogether-Secure.ini", "LipSyncTogether-Open.ini"):
        ini = plugins / "LipSync" / "security" / name
        if not ini.is_file():
            fail(f"missing {ini}")
        (out / "fomod" / "security").mkdir(exist_ok=True)
        shutil.copy2(ini, out / "fomod" / "security" / name)

    # Everything the installer offers exists, and nothing else was packed.
    config = (out / "fomod" / "ModuleConfig.xml").read_text(encoding="utf-8")
    named = {s.replace("\\", "/") for s in re.findall(r'source="([^"]+)"', config)}
    for source in sorted(named):
        if not (out / source).exists():
            fail(f"ModuleConfig.xml names {source}, which was not built")
    for group in ("Launchers", "Servers"):
        for folder in (out / group).iterdir():
            if f"{group}/{folder.name}/SkyrimTogetherReborn" not in named:
                fail(f"{group}/{folder.name} was built but the installer never offers it")
    print(f"assemble_fomod: {out} ({len(named)} sources checked)")


if __name__ == "__main__":
    main()
