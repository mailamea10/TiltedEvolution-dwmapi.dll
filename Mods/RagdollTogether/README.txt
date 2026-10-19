Ragdoll Together (Ragdoll Physics Sync) 1.0.0
=============================================

Shows every Skyrim Together Reborn player the same ragdoll. When a player or an
NPC is knocked down, flung by a shout, killed or knocked out, the body falls,
bounces, comes to rest and gets up on everyone's screen exactly as it does on
the screen of the player who controls it, instead of lying stiff or in a
T-pose.

- the fall, every bounce, arms and hands, and where the body ends up
- the get-up that follows, until the actor is back on its feet
- bodies at rest stay where they are, also for players who arrive later
- players and NPCs alike (whoever controls an actor in Skyrim Together
  decides how it falls)

Requirements (every player in the session)
------------------------------------------
- SKSE for your game version.
- Address Library for SKSE Plugins (Skyrim Together needs it on 1.6.x and
  1.7.x).
- Skyrim Together Reborn, installed as a mod, in one of these versions:

  Skyrim Together              Skyrim
  ---------------------------  ------------------------------
  1.8.3 (official, Nexus)      1.7.104 (main file)
                               1.6.1170 / 1.6.1179 (optional file "for 1.6.1170")
  1.8.5 (absol89)              1.7.104, 1.6.1170 / 1.6.1179
  1.8.5 (1.5.97 port)          1.5.97
  1.8.6 playtest (absol89)     1.7.104, 1.6.1170 / 1.6.1179

- Windows 8 or newer.

The installer first asks which Skyrim Together you use, then your game
version. Not sure? You have the official one from Nexus: pick 1.8.3.

1.8.3, 1.8.5 and 1.8.6 cannot play together (nor 1.8.6 on 1.7.104 with 1.8.6 on
1.6.1170), so everyone in a session uses the same
one. Any regular server of that version works; nothing is installed on the
server.

Good to know: Skyrim Together's server keeps every message mods like this one
send for as long as it runs, and gives them to everyone in the session.
Ragdoll Together keeps its messages small and only sends while a body moves,
roughly 15-30 KB per knockdown or death. On a very long, combat-heavy session
that adds up to some tens of MB of memory on each PC; restarting the server
clears it.

Works together with
- SkyParkour Together, RaceMenu Together and LipSync Together (and every
  later mod of ours with a Skyrim Together add-on): they all ship the same
  Skyrim Together launcher, which carries every one of their add-ons. Install
  them in any order; whichever mod's SkyrimTogether.exe ends up in use runs
  all of them. Keep them up to date together, so the launcher in use is the
  newest one.

Do not use together with
- STRPluginMessagingAPI: it patches Skyrim Together's own launcher and stops
  the launcher in this mod from starting.
- the "Combined test build" (Parkour + RaceMenu + Ragdoll) from the same
  GitHub repository: it is a separate test version of Skyrim Together with
  its own ragdoll sync built in.

What is in the archive
----------------------
- SKSE/Plugins/RagdollTogether.dll: the SKSE plugin. It switches the add-on
  on and writes a log. One DLL for every game version; it does not touch game
  memory.
- SkyrimTogetherReborn/SkyrimTogether.exe: the Skyrim Together launcher for
  your Skyrim Together and game version, built from that release with our
  add-ons. Ragdoll Together's add-on reads the ragdoll of the actors your game
  controls, carries it to the other players and puts it on those actors there.
  The same launcher carries the add-ons of our other mods; each one stays off
  unless its own SKSE plugin is installed. Nothing else in Skyrim Together is
  changed. Docs/Ragdoll Together - launcher manifest.txt lists exactly what
  it was built from.
  The 1.5.97 option also installs EarlyLoad.dll, TPProcess.exe and
  versionlib-ae-to-se-1-5-97-0.map, because Skyrim Together has no 1.5.97
  launcher of its own.

Install
-------
Skyrim Together starts from SkyrimTogetherReborn/SkyrimTogether.exe, and that
launcher only runs from the folder that also holds the rest of Skyrim Together
(libcef.dll, EarlyLoad.dll, the UI and so on). So this mod's launcher has to
end up in that folder; which way depends on your mod manager.

1. Install Skyrim Together Reborn as usual.
2. Install this archive with your mod manager. Pick which Skyrim Together you
   use, then your game version.

Mod Organizer 2
  MO2 starts a launcher from its real folder on disk, not from the merged
  game folder, so mod order does not help here. Copy the launcher by hand:
  3. Right-click Ragdoll Together > Open in Explorer and open its
     SkyrimTogetherReborn folder.
  4. Right-click your Skyrim Together Reborn mod > Open in Explorer and open
     its SkyrimTogetherReborn folder.
  5. In the Skyrim Together folder, rename SkyrimTogether.exe to
     SkyrimTogether.original.exe (your backup).
  6. Copy SkyrimTogether.exe from the Ragdoll Together folder into the
     Skyrim Together folder. On 1.5.97, also copy EarlyLoad.dll, TPProcess.exe
     and versionlib-ae-to-se-1-5-97-0.map, replacing what is there.
  7. Keep Ragdoll Together enabled (it provides the SKSE plugin) and start
     the game with your usual MO2 entry for Skyrim Together, the one that
     points into the Skyrim Together Reborn mod. Do not add an MO2 entry that
     points into the Ragdoll Together mod: that folder only holds the
     launcher, which cannot start from there.

Vortex
  Vortex places all files in the game folder, so mod order is enough:
  3. Set Ragdoll Together to load AFTER Skyrim Together Reborn so its
     SkyrimTogether.exe wins the conflict, and deploy.
  4. Start the game from SkyrimTogetherReborn/SkyrimTogether.exe in the game's
     Data folder, as you normally start Skyrim Together.

Using it with our other mods
  They all ship the identical SkyrimTogether.exe. In MO2, copy it from any one
  of them (steps 3-6 once); in Vortex, the file conflict between them does not
  matter. Every feature works with any copy.

Checking that it works
----------------------
- Documents/My Games/Skyrim Special Edition/SKSE/RagdollTogether.log
  should say "Skyrim Together add-on found".
- Skyrim Together's log (SkyrimTogetherReborn/logs; in MO2 usually the
  overwrite folder) should contain
  "Ragdoll Together: Ragdoll Together 1.0.0 found; ragdolls are shared".

Troubleshooting
- The launcher closes straight away, with no window and no log: it was
  started from a folder without the rest of Skyrim Together. In MO2, follow
  steps 3-7 above.
- The SKSE log says the add-on was not found: the game was started with a
  different SkyrimTogether.exe (Skyrim Together's own, another fork's, or one
  from an older version of our mods). In MO2, check that the copied launcher
  is the one in the Skyrim Together Reborn mod; in Vortex, check the load
  order.
- Skyrim Together's log says "the renderer could not be hooked on this game
  version": your game version is not one of those listed above.
- A body still lies stiff on another player's screen: that player does not
  have Ragdoll Together, or uses a different SkyrimTogether.exe.

Uninstall
---------
Mod Organizer 2: delete the copied SkyrimTogether.exe from the Skyrim Together
Reborn mod and rename SkyrimTogether.original.exe back to SkyrimTogether.exe
(on 1.5.97, restore the other copied files from your Skyrim Together
download), then remove this mod.
Vortex: remove the mod; Skyrim Together's own launcher takes over again.

Credits
-------
- Skyrim Together Reborn by Tilted Phoques and its contributors; the 1.8.5
  release by absol89.
- Ragdoll Together by mailamea10.

License
-------
GNU General Public License v3 (the Skyrim Together launcher is GPLv3, and so
is the add-on and the plugin). Source:
https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch release/ragdoll-together, folder Mods/RagdollTogether)
