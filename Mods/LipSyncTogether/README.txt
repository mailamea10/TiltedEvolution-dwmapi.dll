LipSync Together (Voice Lip Sync) 0.2.0 (pre-release)
=====================================================

Talk to your friends through your characters. While you speak into your
microphone, your character's mouth moves, on your screen and on every other
Skyrim Together Reborn player's screen. The same goes for them on yours.

- Voice only: no webcam, no recording. The microphone is analysed on your PC
  and only small mouth shapes (a few bytes) are sent to the other players.
  Your voice itself is never sent; use your usual voice chat (Discord and so
  on) next to it.
- Only your voice moves the mouth: keyboard clicks, a fan or other room noise
  are ignored, because speech has a pitch and they do not.
- The mouth follows your own speaking volume and your vowels, with Skyrim's
  own phoneme shapes, at about the strength vanilla dialogue uses.
- Optional push-to-talk and mute keys.

Requirements (every player in the session)
------------------------------------------
- SKSE for your game version.
- Mfg Fix NG or Mfg Fix, whichever supports your game version, from its
  own Nexus page. LipSync Together moves the face through Mfg Fix's phoneme
  function. This mod contains no Mfg Fix files.
- Skyrim Together Reborn, installed as a mod, in one of these versions:

  Skyrim Together              Skyrim
  ---------------------------  ------------------------------
  1.8.3 (official, Nexus)      1.7.104 (main file)
                               1.6.1170 / 1.6.1179 (optional file "for 1.6.1170")
  1.8.5 (absol89)              1.7.104, 1.6.1170 / 1.6.1179
  1.8.5 (1.5.97 port)          1.5.97
  1.8.6 playtest (absol89)     1.7.104, 1.6.1170 / 1.6.1179

- A microphone (only for players who want their own mouth to move; players
  without one still see everyone else's).
- Windows 8 or newer.

The installer first asks which Skyrim Together you use, then your game
version. Not sure? You have the official one from Nexus: pick 1.8.3.

1.8.3, 1.8.5 and 1.8.6 cannot play together (nor 1.8.6 on 1.7.104 with 1.8.6 on
1.6.1170), so everyone in a session uses the same
one. Any regular server of that version works; nothing is installed on the
server.

Works together with
- SkyParkour Together, RaceMenu Together (and every later mod of ours with a
  Skyrim Together add-on): they all ship the same Skyrim Together launcher,
  which carries every one of their add-ons. Install them in any order;
  whichever mod's SkyrimTogether.exe ends up in use runs all of them. Keep
  them up to date together, so the launcher in use is the newest one.

Do not use together with
- other lip sync or face sync mods for Skyrim Together that move the same
  characters' phonemes.
- STRPluginMessagingAPI: it patches Skyrim Together's own launcher and stops
  the launcher in this mod from starting.
- the "Combined test build" (Parkour + RaceMenu + Ragdoll) from the same
  GitHub repository: it is a separate test version of Skyrim Together.

What is in the archive
----------------------
- SKSE/Plugins/LipSyncTogether.dll: the SKSE plugin. It listens to your
  microphone (Windows audio, shared, so voice chat keeps working), tells your
  voice apart from noise and turns it into mouth shapes. One DLL for every
  game version; it does not touch game memory. It only opens the microphone
  when the game was started with a Skyrim Together launcher that carries the
  add-on.
- SKSE/Plugins/LipSyncTogether.ini: the settings (see below).
- SkyrimTogetherReborn/SkyrimTogether.exe: the Skyrim Together launcher for
  your Skyrim Together and game version, built from that release with our
  add-ons. LipSync Together's add-on puts the mouth shapes on your face,
  sends them to the other players and puts theirs on their characters. The
  same launcher carries the add-ons of our other mods (such as SkyParkour
  Together and RaceMenu Together); each one stays off unless its own SKSE
  plugin is installed. Nothing else in Skyrim Together is changed.
  Docs/LipSync Together - launcher manifest.txt lists exactly what it was
  built from.
  The 1.5.97 option also installs EarlyLoad.dll, TPProcess.exe and
  versionlib-ae-to-se-1-5-97-0.map, because Skyrim Together has no 1.5.97
  launcher of its own.

Install
-------
Skyrim Together starts from SkyrimTogetherReborn/SkyrimTogether.exe, and that
launcher only runs from the folder that also holds the rest of Skyrim Together
(libcef.dll, EarlyLoad.dll, the UI and so on). So this mod's launcher has to
end up in that folder; which way depends on your mod manager.

1. Install Skyrim Together Reborn and Mfg Fix NG (or Mfg Fix) as usual.
2. Install this archive with your mod manager. Pick which Skyrim Together you
   use, then your game version.

Mod Organizer 2
  MO2 starts a launcher from its real folder on disk, not from the merged
  game folder, so mod order does not help here. Copy the launcher by hand:
  3. Right-click LipSync Together > Open in Explorer and open its
     SkyrimTogetherReborn folder.
  4. Right-click your Skyrim Together Reborn mod > Open in Explorer and open
     its SkyrimTogetherReborn folder.
  5. In the Skyrim Together folder, rename SkyrimTogether.exe to
     SkyrimTogether.original.exe (your backup).
  6. Copy SkyrimTogether.exe from the LipSync Together folder into the
     Skyrim Together folder. On 1.5.97, also copy EarlyLoad.dll, TPProcess.exe
     and versionlib-ae-to-se-1-5-97-0.map, replacing what is there.
  7. Keep LipSync Together enabled (it provides the SKSE plugin) and start
     the game with your usual MO2 entry for Skyrim Together, the one that
     points into the Skyrim Together Reborn mod. Do not add an MO2 entry that
     points into the LipSync Together mod: that folder only holds the
     launcher, which cannot start from there.

Vortex
  Vortex places all files in the game folder, so mod order is enough:
  3. Set LipSync Together to load AFTER Skyrim Together Reborn so its
     SkyrimTogether.exe wins the conflict, and deploy.
  4. Start the game from SkyrimTogetherReborn/SkyrimTogether.exe in the game's
     Data folder, as you normally start Skyrim Together.

Using it with SkyParkour Together or RaceMenu Together
  All of them ship the identical SkyrimTogether.exe. In MO2, copy it from any
  one of them (steps 3-6 once); in Vortex, the file conflict between them does
  not matter. Every feature works with any copy.

Choosing the microphone
-----------------------
By default the Windows default communications microphone is used. Some
virtual or software audio devices pass only silence to other programs. If your mouth does not move:
1. Open Documents/My Games/Skyrim Special Edition/SKSE/LipSyncTogether.log.
   It lists every microphone ("microphone 1: ...") and the one in use
   ("Listening to ..."). If it says the microphone "has sent nothing but
   digital silence", that device is the problem.
2. In LipSyncTogether.ini (SKSE/Plugins, in this mod's folder), set Device=
   to any part of your real microphone's name as the log lists it, and
   restart the game.

Settings (LipSyncTogether.ini)
------------------------------
Device          part of the microphone's name (empty: Windows default)
NoiseFilter     1: only a voice moves the mouth (default); 0: anything loud
VoiceThreshold  how clearly pitched a sound must be to count as a voice
GateDb          how far above the room noise counts as speech
MinLevelDb      anything quieter never counts
Sensitivity     how far the jaw opens
HoldMs          how long the mouth keeps moving after the voice stops
Mode            Shapes (jaw and lips) or Jaw (jaw only)
PushToTalkKey   virtual-key code, for example 0x56 for V (0 = off)
MuteToggleKey   virtual-key code, for example 0x4D for M (0 = off)
AnimateSelf     1: your own character's mouth moves too
MouthStrength   scales every mouth shape (1 = normal)
RemoteDelayMs   how long other players' mouths wait to smooth out the network
MaxDistance     players further away (game units) are left alone
LogLevels       1: log the microphone level and mouth shapes once a second

Every setting is explained in the INI itself. Changes take effect the next
time the game starts. Push-to-talk and mute keys only count while the game
window has the focus.

Checking that it works
----------------------
- Documents/My Games/Skyrim Special Edition/SKSE/LipSyncTogether.log should
  say "The Skyrim Together add-on asked for the mouth; opening the
  microphone" and "Listening to <your microphone>".
- Skyrim Together's log (SkyrimTogetherReborn/logs; in MO2 usually the
  overwrite folder) should contain
  "LipSync Together: found LipSync Together 0.2.0 ..." and
  "LipSync Together: faces are moved with MfgConsoleFunc.SetPhonemeModifier".
- With LogLevels=1 in the INI, LipSyncTogether.log shows your level and
  "speaking" while you talk.

Troubleshooting
- The launcher closes straight away, with no window and no log: it was
  started from a folder without the rest of Skyrim Together. In MO2, follow
  steps 3-7 above.
- LipSyncTogether.log stops at "Waiting for the Skyrim Together add-on": the
  game was started with a different SkyrimTogether.exe (Skyrim Together's own,
  another fork's, or one from an older version of our mods). In MO2, check
  that the copied launcher is the one in the Skyrim Together Reborn mod; in
  Vortex, check the load order.
- Skyrim Together's log says "MfgConsoleFunc.SetPhonemeModifier was never
  registered": Mfg Fix NG (or Mfg Fix) is not installed or did not load.
  Nobody's mouth moves on your screen until it is, but yours is still sent.
- The log shows "level -120.0 dB" or "digital silence": see "Choosing the
  microphone".
- The mouth moves on noise: raise GateDb or VoiceThreshold. It misses quiet
  speech: lower them.
- The mouth is too subtle or too strong: change MouthStrength.
- Another player's mouth runs ahead of their voice in voice chat: raise
  RemoteDelayMs (for example to 150). It lags behind: lower it (down to 0).

Uninstall
---------
Mod Organizer 2: delete the copied SkyrimTogether.exe from the Skyrim Together
Reborn mod and rename SkyrimTogether.original.exe back to SkyrimTogether.exe
(on 1.5.97, restore the other copied files from your Skyrim Together
download), then remove this mod.
Vortex: remove the mod; Skyrim Together's own launcher takes over again.

Privacy
-------
The microphone is only opened while the game runs with the add-on, and is
only analysed on your PC. What goes over the network is a mouth shape (three
characters, a few times a second, only while you speak). No audio is
recorded, stored or sent.

Credits
-------
- Mfg Fix NG and Mfg Fix by their authors. LipSync Together calls Mfg Fix's
  phoneme function; no Mfg Fix files or code are included.
- Skyrim Together Reborn by Tilted Phoques and its contributors; the 1.8.5
  release by absol89.
- LipSync Together by mailamea10.

License
-------
GNU General Public License v3 (the Skyrim Together launcher is GPLv3, and so
is the add-on and the plugin). Source:
https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch release/lipsync-together, folder Mods/LipSyncTogether)
