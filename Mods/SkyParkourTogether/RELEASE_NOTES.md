> **Deprecated:** this release replaces SkyrimTogether.exe with a modified one. A new version that works with official Skyrim Together 1.8.4 and later without replacing any Skyrim Together files is in development and will replace it.

**Supported Skyrim Together:** the official Skyrim Together Reborn 1.8.4 from [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/69993) only, on Skyrim 1.7.104 and 1.6.1170 / 1.6.1179. The launcher is built from Tilted Phoques' v1.8.4 source.

**SkyParkour Together (Parkour Animations Sync) 1.0.1** shares SkyParkour's animations between Skyrim Together Reborn players: vaults, climbs, ledge grabs, steps, slides and landing rolls play on your character for everyone, with the same height and variant you got.

One FOMOD for the official **Skyrim Together Reborn 1.8.4** from [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/69993): Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**. The installer asks for your game version.

**Contents**
- `SKSE/Plugins/SkyParkourTogether.dll`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your game version: the shared launcher every one of our mods ships, that release plus our add-ons (SkyParkour Together's STR Mod Sync add-on carries the animations; RaceMenu Together's shares appearances).

**Requirements (every player):** the official Skyrim Together Reborn 1.8.4 from Nexus, SkyParkour (by Tsptds, not included), SKSE and Address Library for your game version. Works with any regular Skyrim Together 1.8.4 server.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in, and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

**Works with RaceMenu Together:** both mods ship the identical launcher with both add-ons. Not compatible with other Skyrim Together forks or STRPluginMessagingAPI, which replace or patch the launcher.

See the README in the archive for checks, troubleshooting and uninstalling.

**Downloads**
- `SkyParkour_Together_Parkour_Animations_Sync-1.0.1-STR-1.8.4.zip`: **current version, use this one.**

**Changelog**

*1.0.1*
- The Skyrim Together launcher in the archive is now the shared launcher that all of our mods ship, carrying the add-ons of SkyParkour Together and RaceMenu Together. Install both mods in any order and both work; with only one of them installed, the other add-on stays off.
- The plugin and the parkour sync are unchanged. If you use RaceMenu Together too, update to 1.0.1 so the launcher in use carries both add-ons.

*1.0.0*
- First release: SkyParkour's vaults, climbs, ledge grabs, steps, slides and landing rolls shared between Skyrim Together Reborn players, for Skyrim Together 1.8.3 (1.7.104, 1.6.1170/1.6.1179).

SkyParkour Together is an unofficial compatibility add-on, not affiliated with SkyParkour's author. GPLv3; source in `Mods/SkyParkourTogether` on this branch.
