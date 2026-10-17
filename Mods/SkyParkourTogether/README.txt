SkyParkour Together (Parkour Animations Sync) 1.0.0
===================================================

Shares SkyParkour's animations between Skyrim Together Reborn players. When
you vault, climb, grab a ledge, step up, slide or land in a roll, the other
players see the same animation on your character, picked with the same
height and variant you got.

Requirements (every player in the session)
------------------------------------------
- SKSE for your game version, and Address Library for SKSE Plugins
- SkyParkour (by Tsptds), from its own Nexus page. This mod contains no
  SkyParkour files.
- Skyrim Together Reborn, installed as a mod, in one of these versions:

  Skyrim Together              Skyrim
  ---------------------------  ------------------------------
  1.8.3 (official, Nexus)      1.7.104 (main file)
                               1.6.1170 / 1.6.1179 (optional file "for 1.6.1170")
  1.8.5 (absol89)              1.7.104, 1.6.1170 / 1.6.1179
  1.8.5 (1.5.97 port)          1.5.97

The installer first asks which Skyrim Together you use, then your game
version. Not sure? You have the official one from Nexus: pick 1.8.3.

1.8.3 and 1.8.5 cannot play together, so everyone in a session uses the same
one. Any regular server of that version works; nothing is installed on the
server.

What is in the archive
----------------------
- SKSE/Plugins/SkyParkourTogether.dll: the SKSE plugin. It tells Skyrim
  Together which of your animations are SkyParkour's. One DLL for every
  game version; it does not touch game memory.
- SkyrimTogetherReborn/SkyrimTogether.exe: the Skyrim Together launcher for
  your Skyrim Together and game version, built from that release with one small
  add-on (the STR Mod Sync API) that carries the animations to the other
  players. Nothing else in Skyrim Together is changed.
  The 1.5.97 option also installs EarlyLoad.dll, TPProcess.exe and
  versionlib-ae-to-se-1-5-97-0.map, because Skyrim Together has no 1.5.97
  launcher of its own.

Install
-------
Skyrim Together starts from SkyrimTogetherReborn/SkyrimTogether.exe, and that
launcher only runs from the folder that also holds the rest of Skyrim Together
(libcef.dll, EarlyLoad.dll, the UI and so on). So this mod's launcher has to
end up in that folder; which way depends on your mod manager.

1. Install Skyrim Together Reborn and SkyParkour as usual.
2. Install this archive with your mod manager. Pick which Skyrim Together you
   use, then your game version.

Mod Organizer 2
  MO2 starts a launcher from its real folder on disk, not from the merged
  game folder, so mod order does not help here. Copy the launcher by hand:
  3. Right-click SkyParkour Together > Open in Explorer and open its
     SkyrimTogetherReborn folder.
  4. Right-click your Skyrim Together Reborn mod > Open in Explorer and open
     its SkyrimTogetherReborn folder.
  5. In the Skyrim Together folder, rename SkyrimTogether.exe to
     SkyrimTogether.original.exe (your backup).
  6. Copy SkyrimTogether.exe from the SkyParkour Together folder into the
     Skyrim Together folder. On 1.5.97, also copy EarlyLoad.dll, TPProcess.exe
     and versionlib-ae-to-se-1-5-97-0.map, replacing what is there.
  7. Keep SkyParkour Together enabled (it provides the SKSE plugin) and start
     the game with your usual MO2 entry for Skyrim Together, the one that
     points into the Skyrim Together Reborn mod. Do not add an MO2 entry that
     points into the SkyParkour Together mod: that folder only holds the
     launcher, which cannot start from there.

Vortex
  Vortex places all files in the game folder, so mod order is enough:
  3. Set SkyParkour Together to load AFTER Skyrim Together Reborn so its
     SkyrimTogether.exe wins the conflict, and deploy.
  4. Start the game from SkyrimTogetherReborn/SkyrimTogether.exe in the game's
     Data folder, as you normally start Skyrim Together.

Checking that it works
----------------------
- Documents/My Games/Skyrim Special Edition/SKSE/SkyParkourTogether.log
  should say "Skyrim Together add-on found".
- Skyrim Together's log (SkyrimTogetherReborn/logs; in MO2 usually the
  overwrite folder) should contain
  "mod sync: SkyParkour Together 1.0.0 ... registered 1 event source(s)".

Troubleshooting
- The launcher closes straight away, with no window and no log: it was
  started from a folder without the rest of Skyrim Together. In MO2, follow
  steps 3-7 above.
- The SKSE log says the add-on was not found: the game was started with a
  different SkyrimTogether.exe. In MO2, check that the copied launcher is the
  one in the Skyrim Together Reborn mod; in Vortex, check the load order.

Uninstall
---------
Mod Organizer 2: delete the copied SkyrimTogether.exe from the Skyrim Together
Reborn mod and rename SkyrimTogether.original.exe back to SkyrimTogether.exe
(on 1.5.97, restore the other copied files from your Skyrim Together
download), then remove this mod.
Vortex: remove the mod; Skyrim Together's own launcher takes over again.

Credits
-------
- SkyParkour by Tsptds. SkyParkour Together is an unofficial compatibility
  add-on and is not affiliated with or endorsed by the SkyParkour author.
- Skyrim Together Reborn by Tilted Phoques and its contributors; the 1.8.5
  release by absol89.
- SkyParkour Together by mailamea10.

License
-------
GNU General Public License v3 (the Skyrim Together launcher is GPLv3, and so
is the add-on and the plugin). Source:
https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch release/skyparkour-together, folder Mods/SkyParkourTogether)
