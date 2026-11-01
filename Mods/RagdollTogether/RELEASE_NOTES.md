> **Deprecated:** this release replaces SkyrimTogether.exe with a modified one. A new version that works with official Skyrim Together 1.8.4 and later without replacing any Skyrim Together files is in development and will replace it.

**Supported Skyrim Together:** the official Skyrim Together Reborn 1.8.4 from [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/69993) only, on Skyrim 1.7.104 and 1.6.1170 / 1.6.1179. The launcher is built from Tilted Phoques' v1.8.4 source.

**Ragdoll/Physics Together SKSE 1.0.0** (Ragdoll Together) shows every Skyrim Together Reborn player the same ragdoll: when a player or NPC is knocked down, flung by a shout, killed or knocked out, the body falls, bounces, comes to rest and gets up on everyone's screen exactly as on the screen of the player who controls it.

One FOMOD for the official **Skyrim Together Reborn 1.8.4** from [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/69993): Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**. The installer asks for your game version.

**Contents**
- `SKSE/Plugins/RagdollTogether.dll`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your game version: the shared launcher every one of our mods ships, that release plus our add-ons (Ragdoll Together's samples, carries and applies ragdolls; SkyParkour Together's shares parkour; RaceMenu Together's shares appearances; LipSync Together's moves mouths).

**Requirements (every player):** the official Skyrim Together Reborn 1.8.4 from Nexus, SKSE and Address Library for your game version. Works with any regular Skyrim Together 1.8.4 server; the server's memory grows by roughly 15-30 KB per knockdown or death until it restarts.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in, and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

**Works with SkyParkour Together, RaceMenu Together and LipSync Together:** all ship the identical launcher with every add-on, so install them in any order and every feature works. Not compatible with other Skyrim Together forks or STRPluginMessagingAPI, which replace or patch the launcher.

See the README in the archive for checks, troubleshooting and uninstalling.

GPLv3; source in `Mods/RagdollTogether` on this branch.
