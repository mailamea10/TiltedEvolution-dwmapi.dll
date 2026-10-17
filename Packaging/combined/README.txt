Skyrim Together Reborn 1.8.5 - combined test build
Parkour sync (SkyParkour Together) + RaceMenu sync + Ragdoll sync
====================================================================

A test build, not a release. Every player and the server must use files from
the same build, or the server refuses the connection.

What is in it
-------------
- Parkour sync: the SkyParkour Together add-on (STR Mod Sync API) plus its SKSE
  plugin, SkyParkourTogether.dll. Same code as the skyparkour-together-v1.0.0
  release.
- RaceMenu sync: sculpting, RaceMenu presets, overlays, hair color and body
  sliders on the other players.
- Ragdoll sync: the owner's ragdoll (knockdowns, deaths) shown on every client.

Files
-----
combined-1.5.97.zip     Client for Skyrim SE 1.5.97 (the legacy port).
combined-1.6.1170.zip   Client for Skyrim SE 1.6.1170 / 1.6.1179.
combined-server.zip     The matching server. RaceMenu and ragdoll sync add
                        messages that only this server relays, so whoever hosts
                        must run it.

Requirements
------------
- Skyrim Together Reborn 1.8.5 already installed (absol89's build for 1.6.1170, or
  our legacy-test package for 1.5.97).
- SkyParkour (SkyParkourNG) for parkour, RaceMenu for appearance sync.
- Do not use STRPluginMessagingAPI with this build: it patches the stock
  launcher and closes the game with ours.
- Disable or remove the separate SkyParkour Together mod while testing this
  build. This build already contains it, so keeping both installs the add-on twice.

Install (MO2)
-------------
1. Install the client zip for your game version as a new mod, and enable it.
   It installs SKSE\Plugins\SkyParkourTogether.dll.
2. MO2 runs SkyrimTogether.exe from its real folder, so the launcher has to sit
   next to Skyrim Together's own files.
   a. Open the Skyrim Together Reborn mod's folder (right click > Open in
      Explorer) and go into SkyrimTogetherReborn.
   b. Rename SkyrimTogether.exe to SkyrimTogether.original.exe (keep it).
   c. Copy SkyrimTogether.exe from this build's SkyrimTogetherReborn folder in
      there.
   d. For 1.5.97 also copy EarlyLoad.dll, TPProcess.exe and the
      versionlib-ae-to-se-1-5-97-0.map file (back up EarlyLoad.dll and
      TPProcess.exe first).
3. Start the game with your existing SkyrimTogether executable entry in MO2.

Install (Vortex)
----------------
Install the client zip and set it to load after Skyrim Together Reborn, then
deploy.

Server
------
Extract combined-server.zip over your server folder (back up STServer.dll and
SkyrimTogetherServer.exe first), or run it from its own folder.

Logs
----
- Client: SkyrimTogetherReborn\logs\tp_client.log ("mod sync:", "RaceMenu sync:",
  "Ragdoll sync:" lines).
- Plugin: Documents\My Games\Skyrim Special Edition\SKSE\SkyParkourTogether.log.

Uninstall
---------
Disable the mod, and rename SkyrimTogether.original.exe back to SkyrimTogether.exe
(plus EarlyLoad.dll and TPProcess.exe for 1.5.97).
