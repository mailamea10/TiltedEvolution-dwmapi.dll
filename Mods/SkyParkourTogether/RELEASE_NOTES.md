**SkyParkour Together (Parkour Animations Sync) 1.0.0** shares SkyParkour's animations between Skyrim Together Reborn 1.8.5 players: vaults, climbs, ledge grabs, steps, slides and landing rolls play on your character for everyone, with the same height and variant you got.

One FOMOD for the official **Skyrim Together Reborn 1.8.3** from Nexus (Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**) and **Skyrim Together Reborn 1.8.5** (Skyrim 1.7.104, 1.6.1170 / 1.6.1179 and **1.5.97**). The installer asks which Skyrim Together you use, then your game version.

**Contents**
- `SKSE/Plugins/SkyParkourTogether.dll`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your Skyrim Together and game version: that release plus the STR Mod Sync add-on that carries the animations. The 1.5.97 option adds the 1.5.97 launcher files Skyrim Together needs on that version.

**Requirements (every player):** Skyrim Together Reborn 1.8.3 or 1.8.5, SkyParkour (by Tsptds, not included), SKSE and Address Library for your game version. Works with any regular server of your Skyrim Together version.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in (on 1.5.97 also `EarlyLoad.dll`, `TPProcess.exe` and the `.map`), and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

See the README in the archive for checks, troubleshooting and uninstalling.

SkyParkour Together is an unofficial compatibility add-on, not affiliated with SkyParkour's author. GPLv3; source in `Mods/SkyParkourTogether` on this branch.
