**Ragdoll Together (Ragdoll Physics Sync) 1.0.0** shows every Skyrim Together Reborn player the same ragdoll: when a player or NPC is knocked down, flung by a shout, killed or knocked out, the body falls, bounces, comes to rest and gets up on everyone's screen exactly as on the screen of the player who controls it.

One FOMOD for the official **Skyrim Together Reborn 1.8.3** from Nexus (Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**) and **Skyrim Together Reborn 1.8.5** (Skyrim 1.7.104, 1.6.1170 / 1.6.1179 and **1.5.97**), and the **Skyrim Together Reborn 1.8.6 playtest** by absol89 (Skyrim 1.7.104 and 1.6.1170 / 1.6.1179). The installer asks which Skyrim Together you use, then your game version.

**Contents**
- `SKSE/Plugins/RagdollTogether.dll`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your Skyrim Together and game version: the shared launcher every one of our mods ships, that release plus our add-ons (Ragdoll Together's samples, carries and applies ragdolls; SkyParkour Together's shares parkour; RaceMenu Together's shares appearances; LipSync Together's moves mouths). The 1.5.97 option adds the 1.5.97 launcher files Skyrim Together needs on that version.

**Requirements (every player):** Skyrim Together Reborn 1.8.3, 1.8.5 or the 1.8.6 playtest, SKSE and Address Library for your game version. Works with any regular server of your Skyrim Together version; the server's memory grows by roughly 15-30 KB per knockdown or death until it restarts.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in (on 1.5.97 also `EarlyLoad.dll`, `TPProcess.exe` and the `.map`), and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

**Works with SkyParkour Together, RaceMenu Together and LipSync Together:** all ship the identical launcher with every add-on, so install them in any order and every feature works. Not compatible with other Skyrim Together forks or STRPluginMessagingAPI, which replace or patch the launcher, or with our separate "Combined test build", which has its own ragdoll sync.

See the README in the archive for checks, troubleshooting and uninstalling.

GPLv3; source in `Mods/RagdollTogether` on this branch.
