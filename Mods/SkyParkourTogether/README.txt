SkyParkour Together (Parkour Animations Sync) 1.0.0
===================================================

Shares SkyParkour's animations between Skyrim Together Reborn players. When
you vault, climb, grab a ledge, step up, slide or land in a roll, the other
players see the same animation on your character, picked with the same
height and variant you got.

Requirements (every player in the session)
------------------------------------------
- Skyrim Special/Anniversary Edition 1.5.97, 1.6.1170, 1.6.1179 or 1.7.104
- SKSE for that game version, and Address Library for SKSE Plugins
- Skyrim Together Reborn 1.8.5 installed as a mod
- SkyParkour (by Tsptds), from its own Nexus page. This mod contains no
  SkyParkour files.

Any regular Skyrim Together Reborn 1.8.5 server works; nothing is installed
on the server.

What is in the archive
----------------------
- SKSE/Plugins/SkyParkourTogether.dll: the SKSE plugin. It tells Skyrim
  Together which of your animations are SkyParkour's. One DLL for every
  game version; it does not touch game memory.
- SkyrimTogetherRuntime/SkyrimTogether.exe: the Skyrim Together launcher for
  your game version, built from Skyrim Together Reborn 1.8.5 with one small
  add-on (the STR Mod Sync API) that carries the animations to the other
  players. Nothing else in Skyrim Together is changed.
  The 1.5.97 option also installs EarlyLoad.dll, TPProcess.exe and
  versionlib-ae-to-se-1-5-97-0.map, because Skyrim Together has no 1.5.97
  launcher of its own.

Install
-------
1. Install Skyrim Together Reborn 1.8.5 and SkyParkour as usual.
2. Install this archive with your mod manager and pick your game version.
3. Place it AFTER Skyrim Together Reborn so its SkyrimTogether.exe wins the
   conflict (Mod Organizer 2: lower in the left pane; Vortex: load after).
4. Start the game from SkyrimTogetherRuntime/SkyrimTogether.exe as you
   normally start Skyrim Together.

Checking that it works
----------------------
- Documents/My Games/Skyrim Special Edition/SKSE/SkyParkourTogether.log
  should say "Skyrim Together add-on found".
- Skyrim Together's log (SkyrimTogetherRuntime/logs) should contain
  "mod sync: SkyParkour Together 1.0.0 ... registered 1 event source(s)".

If the SKSE log says the add-on was not found, the game was started with a
SkyrimTogether.exe from another mod: check the conflict order (step 3).

Uninstall
---------
Remove the mod. Skyrim Together's own SkyrimTogether.exe takes over again.

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
