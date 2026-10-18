**RaceMenu Together (RaceMenu Appearance Sync) 1.0.0** shares your RaceMenu appearance between Skyrim Together Reborn players: sculpting (including imported heads), RaceMenu's sliders, body morphs (UBE and other custom races), node transforms, overlays and loaded presets show on your character for everyone.

One FOMOD for the official **Skyrim Together Reborn 1.8.3** from Nexus (Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**) and **Skyrim Together Reborn 1.8.5** (Skyrim 1.7.104, 1.6.1170 / 1.6.1179 and **1.5.97**). The installer asks which Skyrim Together you use, then your game version.

**Contents**
- `SKSE/Plugins/RaceMenuTogether.dll`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your Skyrim Together and game version: that release plus the RaceMenu Together add-on that exports, carries and applies appearances. The 1.5.97 option adds the 1.5.97 launcher files Skyrim Together needs on that version.

**Requirements (every player):** Skyrim Together Reborn 1.8.3 or 1.8.5, RaceMenu for your game version (by expired6978, not included), SKSE and Address Library for your game version, and the same appearance mods (bodies such as UBE, hair, skins, BodySlide builds with Build Morphs) as the players you play with. Works with any regular server of your Skyrim Together version.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in (on 1.5.97 also `EarlyLoad.dll`, `TPProcess.exe` and the `.map`), and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

Only one `SkyrimTogether.exe` can be used, so this release does not run at the same time as SkyParkour Together, another Skyrim Together fork, or STRPluginMessagingAPI.

See the README in the archive for checks, troubleshooting and uninstalling.

RaceMenu Together is an unofficial compatibility add-on, not affiliated with RaceMenu's author. GPLv3; source in `Mods/RaceMenuTogether` on this branch.
