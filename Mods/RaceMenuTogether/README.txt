RaceMenu Together (RaceMenu Appearance Sync) 1.0.0
==================================================

Shares your RaceMenu appearance between Skyrim Together Reborn players. What
you set up in RaceMenu shows on your character on everyone else's screen:

- sculpting, including heads imported on the sculpt tab
- RaceMenu's own sliders (face, body, extra sliders from other mods)
- body morphs, including custom races such as UBE
- node transforms (height, bone scales)
- overlays (tattoos, makeup, body paint) and the rest of a RaceMenu preset,
  including one loaded from the preset tab

Your appearance is sent when you join, when you close RaceMenu, and again
whenever another player joins the session.

Requirements (every player in the session)
------------------------------------------
- SKSE for your game version.
- Address Library for SKSE Plugins (RaceMenu needs it on 1.6.x and 1.7.x).
- RaceMenu for your game version (by expired6978), from its own Nexus page.
  This mod contains no RaceMenu files.
- Skyrim Together Reborn, installed as a mod, in one of these versions:

  Skyrim Together              Skyrim
  ---------------------------  ------------------------------
  1.8.3 (official, Nexus)      1.7.104 (main file)
                               1.6.1170 / 1.6.1179 (optional file "for 1.6.1170")
  1.8.5 (absol89)              1.7.104, 1.6.1170 / 1.6.1179
  1.8.5 (1.5.97 port)          1.5.97
  1.8.6 playtest (absol89)     1.7.104, 1.6.1170 / 1.6.1179

- The same appearance mods as the player you look at. A preset only names
  meshes, textures, head parts and morphs; it does not carry them. If a
  player uses UBE, a hair mod, a skin texture or BodySlide body morphs, every
  player needs that mod installed (and the body built in BodySlide with
  "Build Morphs" ticked) to see it.
- Windows 8 or newer.

The installer first asks which Skyrim Together you use, then your game
version. Not sure? You have the official one from Nexus: pick 1.8.3.

1.8.3, 1.8.5 and 1.8.6 cannot play together (nor 1.8.6 on 1.7.104 with 1.8.6 on
1.6.1170), so everyone in a session uses the same
one. Any regular server of that version works; nothing is installed on the
server.

Works together with
- SkyParkour Together (and every later mod of ours with a Skyrim Together
  add-on): they all ship the same Skyrim Together launcher, which carries
  every one of their add-ons. Install them in any order; whichever mod's
  SkyrimTogether.exe ends up in use runs all of them. Keep them up to date
  together, so the launcher in use is the newest one.

Do not use together with
- other RaceMenu or body morph sync mods for Skyrim Together (for example
  MorphSyncTogether): both would load appearances onto the same characters.
- STRPluginMessagingAPI: it patches Skyrim Together's own launcher and stops
  the launcher in this mod from starting.
- the "Combined test build" (Parkour + RaceMenu + Ragdoll) from the same
  GitHub repository: it is a separate test version of Skyrim Together that
  already has RaceMenu sync built in.

What is in the archive
----------------------
- SKSE/Plugins/RaceMenuTogether.dll: the SKSE plugin. It switches the
  add-on on, tells it which body morph keys belong to BodyGen and OBody, and
  writes a log. One DLL for every game version; it does not touch game
  memory.
- SkyrimTogetherReborn/SkyrimTogether.exe: the Skyrim Together launcher for
  your Skyrim Together and game version, built from that release with our
  add-ons. RaceMenu Together's add-on exports your appearance with RaceMenu,
  carries it to the other players and loads it onto your character there.
  The same launcher carries the add-ons of our other mods (such as SkyParkour
  Together); each one stays off unless its own SKSE plugin is installed.
  Nothing else in Skyrim Together is changed. Docs/RaceMenu Together -
  launcher manifest.txt lists exactly what it was built from.
  The 1.5.97 option also installs EarlyLoad.dll, TPProcess.exe and
  versionlib-ae-to-se-1-5-97-0.map, because Skyrim Together has no 1.5.97
  launcher of its own.

Install
-------
Skyrim Together starts from SkyrimTogetherReborn/SkyrimTogether.exe, and that
launcher only runs from the folder that also holds the rest of Skyrim Together
(libcef.dll, EarlyLoad.dll, the UI and so on). So this mod's launcher has to
end up in that folder; which way depends on your mod manager.

1. Install Skyrim Together Reborn and RaceMenu as usual.
2. Install this archive with your mod manager. Pick which Skyrim Together you
   use, then your game version.

Mod Organizer 2
  MO2 starts a launcher from its real folder on disk, not from the merged
  game folder, so mod order does not help here. Copy the launcher by hand:
  3. Right-click RaceMenu Together > Open in Explorer and open its
     SkyrimTogetherReborn folder.
  4. Right-click your Skyrim Together Reborn mod > Open in Explorer and open
     its SkyrimTogetherReborn folder.
  5. In the Skyrim Together folder, rename SkyrimTogether.exe to
     SkyrimTogether.original.exe (your backup).
  6. Copy SkyrimTogether.exe from the RaceMenu Together folder into the
     Skyrim Together folder. On 1.5.97, also copy EarlyLoad.dll, TPProcess.exe
     and versionlib-ae-to-se-1-5-97-0.map, replacing what is there.
  7. Keep RaceMenu Together enabled (it provides the SKSE plugin) and start
     the game with your usual MO2 entry for Skyrim Together, the one that
     points into the Skyrim Together Reborn mod. Do not add an MO2 entry that
     points into the RaceMenu Together mod: that folder only holds the
     launcher, which cannot start from there.

Vortex
  Vortex places all files in the game folder, so mod order is enough:
  3. Set RaceMenu Together to load AFTER Skyrim Together Reborn so its
     SkyrimTogether.exe wins the conflict, and deploy.
  4. Start the game from SkyrimTogetherReborn/SkyrimTogether.exe in the game's
     Data folder, as you normally start Skyrim Together.

Using it with SkyParkour Together
  Both mods ship the identical SkyrimTogether.exe. In MO2, copy it from either
  mod (steps 3-6 once); in Vortex, the file conflict between them does not
  matter. Both features work with either copy.

Checking that it works
----------------------
- Documents/My Games/Skyrim Special Edition/SKSE/RaceMenuTogether.log
  should say "Skyrim Together add-on found" and "RaceMenu found".
- Skyrim Together's log (SkyrimTogetherReborn/logs; in MO2 usually the
  overwrite folder) should contain
  "RaceMenu Together: RaceMenu Together 1.0.0 ... found",
  then, in a session, "sending the player's appearance (joined)",
  "received the appearance of player ..." and
  "appearance revision ... applied on ...".

Troubleshooting
- The launcher closes straight away, with no window and no log: it was
  started from a folder without the rest of Skyrim Together. In MO2, follow
  steps 3-7 above.
- The SKSE log says the add-on was not found: the game was started with a
  different SkyrimTogether.exe (Skyrim Together's own, another fork's, or one
  from an older version of our mods). In MO2, check that the copied launcher is the
  one in the Skyrim Together Reborn mod; in Vortex, check the load order.
- Skyrim Together's log says "RaceMenu Together is off": RaceMenu is not
  installed or did not load (check RaceMenu's own requirements).
- A body part, hair or outfit is missing or invisible on another player: you
  do not have the mod (or the BodySlide build) that player uses. See the
  requirements.

Uninstall
---------
Mod Organizer 2: delete the copied SkyrimTogether.exe from the Skyrim Together
Reborn mod and rename SkyrimTogether.original.exe back to SkyrimTogether.exe
(on 1.5.97, restore the other copied files from your Skyrim Together
download), then remove this mod.
Vortex: remove the mod; Skyrim Together's own launcher takes over again.

Credits
-------
- RaceMenu by expired6978. RaceMenu Together is an unofficial compatibility
  add-on and is not affiliated with or endorsed by the RaceMenu author. It
  uses RaceMenu's own preset functions; no RaceMenu files or code are
  included.
- Skyrim Together Reborn by Tilted Phoques and its contributors; the 1.8.5
  release by absol89.
- RaceMenu Together by mailamea10.

License
-------
GNU General Public License v3 (the Skyrim Together launcher is GPLv3, and so
is the add-on and the plugin). Source:
https://github.com/mailamea10/TiltedEvolution-dwmapi.dll
(branch release/racemenu-together, folder Mods/RaceMenuTogether)
