**LipSync Together (Voice Lip Sync) 0.2.0 — pre-release.** Talk to your friends through your characters: while you speak into your microphone, your character's mouth moves on your screen and on every other Skyrim Together Reborn player's screen, and theirs on yours.

- Voice only, no webcam. The microphone is analysed on your PC; only small mouth shapes go over the network, never audio. Use your usual voice chat next to it.
- Only your voice moves the mouth: keyboard clicks, fans and room noise are ignored (speech has a pitch, they do not).
- The mouth follows your own speaking volume and vowels with Skyrim's own phonemes, at about vanilla dialogue strength. Optional push-to-talk and mute keys; everything is set in `LipSyncTogether.ini`.

One FOMOD for the official **Skyrim Together Reborn 1.8.3** from Nexus (Skyrim **1.7.104** and **1.6.1170 / 1.6.1179**) and **Skyrim Together Reborn 1.8.5** (Skyrim 1.7.104, 1.6.1170 / 1.6.1179 and **1.5.97**), and the **Skyrim Together Reborn 1.8.6 playtest** by absol89 (Skyrim 1.7.104 and 1.6.1170 / 1.6.1179). The installer asks which Skyrim Together you use, then your game version.

**Contents**
- `SKSE/Plugins/LipSyncTogether.dll` and `LipSyncTogether.ini`: one SKSE plugin for every runtime (it uses no game addresses).
- `SkyrimTogetherReborn/SkyrimTogether.exe` for your Skyrim Together and game version: the shared launcher every one of our mods ships, that release plus our add-ons (LipSync Together's moves and shares mouths; SkyParkour Together's shares parkour; RaceMenu Together's shares appearances). The 1.5.97 option adds the 1.5.97 launcher files Skyrim Together needs on that version.

**Requirements (every player):** Skyrim Together Reborn 1.8.3, 1.8.5 or the 1.8.6 playtest, SKSE for your game version, and Mfg Fix NG or Mfg Fix for your game version (not included). A microphone only for players who want their own mouth to move. Works with any regular server of your Skyrim Together version.

**Installing:** the launcher has to sit in the same folder as the rest of Skyrim Together.
- **Mod Organizer 2:** after installing, rename `SkyrimTogether.exe` in your Skyrim Together Reborn mod's `SkyrimTogetherReborn` folder to `SkyrimTogether.original.exe`, copy this mod's `SkyrimTogetherReborn/SkyrimTogether.exe` in (on 1.5.97 also `EarlyLoad.dll`, `TPProcess.exe` and the `.map`), and start the game with your usual Skyrim Together entry. Pointing MO2 at the exe inside this mod does not work: it closes straight away.
- **Vortex:** load this mod after Skyrim Together Reborn and deploy.

**Microphone:** if your mouth does not move, open `Documents/My Games/Skyrim Special Edition/SKSE/LipSyncTogether.log`. Some virtual or software audio devices pass only silence; set `Device=` in the INI to part of your real microphone's name.

**Works with SkyParkour Together and RaceMenu Together:** all ship the identical launcher with every add-on, so install them in any order and every feature works. Not compatible with other Skyrim Together forks or STRPluginMessagingAPI, which replace or patch the launcher.

See the README in the archive for settings, checks, troubleshooting and uninstalling.

LipSync Together is an unofficial add-on, not affiliated with Mfg Fix's authors. GPLv3; source in `Mods/LipSyncTogether` on this branch.
