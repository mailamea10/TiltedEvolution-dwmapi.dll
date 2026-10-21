#!/usr/bin/env python3
"""Apply the pinned, non-duplicate AllTogether source patches to STR 1.8.7."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PATCHES = ROOT / "patches"


def apply_patch(source: Path, name: str, *includes: str) -> None:
    patch = PATCHES / name
    command = ["git", "apply"]
    for include in includes:
        command.append(f"--include={include}")
    command.extend(["--check", str(patch)])
    subprocess.run(command, cwd=source, check=True)
    command.remove("--check")
    subprocess.run(command, cwd=source, check=True)


def update_invite_name(source: Path) -> None:
    path = source / "Code/skyrim_ui/src/app/services/player-list.service.ts"
    content = path.read_text(encoding="utf-8")
    old = """          this.popupNotificationService.addPartyInvite(
            invitingPlayer.name,
            () => {
              // A popup for a replaced invite must not accept the newer one.
"""
    new = """          this.popupNotificationService.addPartyInvite(
            // Inviter is not in our party yet; honor global and per-name privacy.
            this.settingService.isNameRevealed(invitingPlayer.name, false)
              ? invitingPlayer.name
              : this.translocoService.translate(
                  'COMPONENT.PARTY_LIST.STRANGER',
                ),
            () => {
              // A popup for a replaced invite must not accept the newer one.
"""
    if new in content:
        return
    if old not in content:
        raise RuntimeError("Could not find the 1.8.7 party invite callback anchor")
    path.write_text(content.replace(old, new, 1), encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--skyrim-version", choices=("1.6.1170", "1.7.104"), required=True)
    args = parser.parse_args()
    source = args.source.resolve()

    apply_patch(
        source,
        "pr931-hide-strangers.patch",
        "Code/skyrim_ui/src/app/components/settings/settings.component.html",
        "Code/skyrim_ui/src/app/services/player-ui.service.ts",
        "Code/skyrim_ui/src/app/services/setting.service.ts",
        "Code/skyrim_ui/src/assets/i18n/*",
    )
    update_invite_name(source)
    apply_patch(source, "pr931-unknown-names.patch")
    if args.skyrim_version == "1.6.1170":
        apply_patch(
            source,
            "e1de-skyrim-version.patch",
            "Code/client/Games/Skyrim/Misc/GameVM.h",
            "Code/client/Games/Skyrim/PlayerCharacter.h",
            "Code/client/main.cpp",
            "Code/client/xmake.lua",
            "xmake.lua",
        )


if __name__ == "__main__":
    main()
