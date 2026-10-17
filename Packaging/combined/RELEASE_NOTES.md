**Test build. Parkour sync (SkyParkour Together), RaceMenu sync and Ragdoll sync in one complete Skyrim Together Reborn 1.8.5.**

## Which file do I download?

**Only `SkyrimTogetherReborn-Combined-Test.zip`.** It is a mod-manager installer (FOMOD) that asks for your Skyrim version (**1.6.1170 / 1.6.1179** or **1.5.97**) and contains everything: launcher, UI, server and the SkyParkour Together plugin. `INSTALL-README.txt` is the same guide as below, for reading on the page.

## Install (Mod Organizer 2)

1. Turn off: your current **Skyrim Together Reborn** mod, the separate **SkyParkour Together** mod, **STRPluginMessagingAPI**, and for Nucleus split screen **SmoothCam**.
2. Install `SkyrimTogetherReborn-Combined-Test.zip` (CD icon, "Install a new mod from an archive").
3. In the installer pick your game: **Skyrim SE 1.6.1170 / 1.6.1179** (EldenRim Together) or **Skyrim SE 1.5.97**.
4. Enable the new mod.
5. Gears icon (Modify Executables) → your SkyrimTogether entry → **Binary**: `mods\<new mod>\SkyrimTogetherReborn\SkyrimTogether.exe`, **Start in**: that same folder → Apply.
6. Launch with that entry; right Ctrl in game opens the Skyrim Together menu.

## Install (Vortex)

Install the zip, pick your Skyrim version, disable the old Skyrim Together Reborn and SkyParkour Together mods, Deploy, then start `Data\SkyrimTogetherReborn\SkyrimTogether.exe`.

## Server / Nucleus Co-op

- Whoever hosts runs `SkyrimTogetherServer.exe` from the new mod's `SkyrimTogetherReborn` folder. Every player must use this build.
- Nucleus: re-add the game and point it at the new mod's `SkyrimTogetherReborn\SkyrimTogether.exe`.

## Check it works

`tp_client.log` shows `mod sync:`, `RaceMenu sync:` and `Ragdoll sync:` lines; `SKSE\SkyParkourTogether.log` says "Skyrim Together add-on found".

Requirements: SKSE for your game version, SkyParkour (+ Pandora/Nemesis run) for parkour, RaceMenu for appearance sync.
