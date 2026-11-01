SkyParkour Together (Parkour Animations Sync) 1.0.1
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
- Skyrim Together Reborn, installed as a mod, in this version:

  Skyrim Together              Skyrim
  ---------------------------  ------------------------------
  1.8.4 (official, Nexus)      1.7.104
                               1.6.1170 / 1.6.1179

The installer asks for your game version. Skyrim Together Reborn 1.8.4 from
Nexus is the supported version.

Everyone in a session uses 1.8.4. Any regular 1.8.4 server works; nothing is installed on the
server.

Works together with
- RaceMenu Together (and every later mod of ours with a Skyrim Together
  add-on): they all ship the same Skyrim Together launcher, which carries
  every one of their add-ons. Install them in any order; whichever mod's
  SkyrimTogether.exe ends up in use runs all of them. Keep them up to date
  together, so the launcher in use is the newest one.

What is in the archive
----------------------
- SKSE/Plugins/SkyParkourTogether.dll: the SKSE plugin. It tells Skyrim
  Together which of your animations are SkyParkour's. One DLL for every
  game version; it does not touch game memory.
- SkyrimTogetherReborn/SkyrimTogether.exe: the Skyrim Together launcher for
  your Skyrim Together and game version, built from that release with our
  add-ons. SkyParkour Together's add-on (the STR Mod Sync API) carries the
  animations to the other players. The same launcher carries the add-ons of
  our other mods (such as RaceMenu Together); each one stays off unless its own
  SKSE plugin is installed. Nothing else in Skyrim Together is changed.
  Docs/SkyParkour Together - launcher manifest.txt lists exactly what it was
  built from.

Install
-------
Skyrim Together starts from SkyrimTogetherReborn/SkyrimTogether.exe, and that
launcher only runs from the folder that also holds the rest of Skyrim Together
(libcef.dll, EarlyLoad.dll, the UI and so on). So this mod's launcher has to
end up in that folder; which way depends on your mod manager.

1. Install Skyrim Together Reborn and SkyParkour as usual.
2. Install this archive with your mod manager. Pick your game version.

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
     Skyrim Together folder.
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
  "mod sync: SkyParkour Together 1.0.1 ... registered 1 event source(s)".

Troubleshooting
- The launcher closes straight away, with no window and no log: it was
  started from a folder without the rest of Skyrim Together. In MO2, follow
  steps 3-7 above.
- The SKSE log says the add-on was not found: the game was started with a
  different SkyrimTogether.exe (Skyrim Together's own, another fork's, or one
  from an older version of our mods). In MO2, check that the copied launcher is the
  one in the Skyrim Together Reborn mod; in Vortex, check the load order.

Uninstall
---------
Mod Organizer 2: delete the copied SkyrimTogether.exe from the Skyrim Together
Reborn mod and rename SkyrimTogether.original.exe back to SkyrimTogether.exe, then remove this mod.
Vortex: remove the mod; Skyrim Together's own launcher takes over again.

Credits
-------
- SkyParkour by Tsptds. SkyParkour Together is an unofficial compatibility
  add-on and is not affiliated with or endorsed by the SkyParkour author.
- Skyrim Together Reborn by Tilted Phoques and its contributors.
- SkyParkour Together by mailamea10.

License
-------
GNU General Public License v3 (the Skyrim Together launcher is GPLv3, and so
is the add-on and the plugin). Source:
https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch release/skyparkour-together, folder Mods/SkyParkourTogether)
