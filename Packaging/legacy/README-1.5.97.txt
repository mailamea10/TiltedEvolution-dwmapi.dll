Skyrim Together Reborn 1.8.5 for Skyrim Special Edition 1.5.97
===============================================================

This is Skyrim Together Reborn 1.8.5 (absol89's TiltedEvolutionScriptFixes)
built for the 1.5.97 game executable. It plays with the regular STR 1.8.5
server, so players on 1.5.97 and on 1.6/1.7 can join the same server.

Requirements
------------
- Skyrim Special Edition 1.5.97 (SkyrimSE.exe 1.5.97.0)
- SKSE64 2.0.20
- Address Library for SKSE Plugins, "All in one" (SE) - provides
  Data/SKSE/Plugins/version-1-5-97-0.bin
- The Skyrim Together Reborn 1.8.5 mod files (esp, scripts, behaviours, the
  SkyrimTogetherReborn folder with its UI) installed as usual

Two downloads
-------------
- SkyrimTogetherReborn-1.8.5-SE-1.5.97.zip: the launcher without a console.
- SkyrimTogetherReborn-1.8.5-SE-1.5.97-console.zip: the same launcher with a
  console window showing the log as it runs, for troubleshooting.
Install one of them; the other files are identical.

Install
-------
The launcher only starts from the folder that holds the rest of Skyrim
Together (libcef.dll, the UI and so on), so these files have to end up in
your Skyrim Together mod's SkyrimTogetherReborn folder.

1. Install Skyrim Together Reborn 1.8.5 as a mod in your mod manager.
2. Mod Organizer 2: right-click the Skyrim Together Reborn mod > Open in
   Explorer, open its SkyrimTogetherReborn folder, back up SkyrimTogether.exe,
   EarlyLoad.dll and TPProcess.exe (rename them, e.g. to *.original), then
   extract this archive's SkyrimTogetherReborn folder over it. MO2 starts a
   launcher from its real folder, so installing this archive as a separate mod
   placed after Skyrim Together does not work, and an MO2 entry pointing into
   such a separate mod closes straight away.
   Vortex: install this archive as a mod that loads after Skyrim Together
   Reborn and deploy.
   Either way it replaces SkyrimTogetherReborn/SkyrimTogether.exe,
   EarlyLoad.dll and TPProcess.exe and adds
   SkyrimTogetherReborn/versionlib-ae-to-se-1-5-97-0.map.
3. Do not install SkyrimTogetherSKSE.dll or SkyrimTogetherRuntime_1_5.dll from
   test builds; the launcher does not need them.
4. In Mod Organizer 2, add the Skyrim Together Reborn mod's
   SkyrimTogetherReborn/SkyrimTogether.exe as an executable and launch it from
   MO2 (Vortex: launch it as a tool). The first start asks for the game folder.

Nothing is written to the game's install folder. Logs (and a crash dump, if
the game crashes) go to SkyrimTogetherReborn/logs, which in MO2 ends up in
the overwrite folder.

Credits
-------
Tilted Phoques and the Skyrim Together Reborn team, absol89
(TiltedEvolutionScriptFixes), Ethanfate1 (SkyrimTogetherNext), miredirex.
1.5.97 port: mailamea10.

License
-------
Skyrim Together Reborn is licensed under the GNU General Public License v3.
Source for this build: https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch feature/legacy-1.5.97-on-1.8.5).
