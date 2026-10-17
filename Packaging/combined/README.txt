Skyrim Together Reborn 1.8.5 - Combined Test Build
Parkour sync (SkyParkour Together) + RaceMenu sync + Ragdoll sync
=================================================================

This is a test build, not a release. It is a COMPLETE Skyrim Together Reborn:
launcher, UI, server, plugins and the SkyParkour Together plugin, all in one
mod. You do not need any other Skyrim Together download.

Every player must install this same build, and whoever hosts must run the
server that comes with it (SkyrimTogetherServer.exe in the same folder).
Otherwise the server refuses the connection.


WHAT YOU NEED
-------------
- Skyrim Special Edition 1.6.1170 / 1.6.1179, or 1.5.97.
- SKSE for your game version (2.2.6 for 1.6.1170, 2.0.20 for 1.5.97).
- SkyParkour (SkyParkourNG) for parkour, and RaceMenu for appearance sync.
  Run Pandora (or Nemesis) after installing SkyParkour.


BEFORE YOU START
----------------
Turn these off if you have them. They clash with this build:
- Your current Skyrim Together Reborn mod (this build replaces it).
- The separate "SkyParkour Together" mod (this build already contains it).
- STRPluginMessagingAPI (it closes the game with this launcher).
- For Nucleus Co-op split screen: SmoothCam.


INSTALL WITH MOD ORGANIZER 2
----------------------------
1. Download the one zip from the release page
   (SkyrimTogetherReborn-Combined-Test.zip).
2. In MO2 click "Install a new mod from an archive" (the CD icon) and pick the
   zip.
3. The installer asks which Skyrim version you play:
     - Skyrim SE 1.6.1170 / 1.6.1179  -> EldenRim Together and most current
       modlists.
     - Skyrim SE 1.5.97              -> the legacy 1.5.97 port.
   Pick yours and finish the install.
4. Enable the new mod in the left pane. Disable your old Skyrim Together
   Reborn mod and the old SkyParkour Together mod.
5. Point MO2's SkyrimTogether executable at the new mod:
     a. Click the two gears ("Modify Executables").
     b. Select your SkyrimTogether entry (or press + and add a new one).
     c. Binary: browse to
          <MO2 folder>\mods\<this mod's name>\SkyrimTogetherReborn\SkyrimTogether.exe
     d. Start in: the same SkyrimTogetherReborn folder.
     e. Apply.
6. Launch with that entry. In game press the right Ctrl key to open the
   Skyrim Together menu and connect.


INSTALL WITH VORTEX
-------------------
1. Drag the zip into Vortex's Mods page and install it.
2. Pick your Skyrim version in the installer.
3. Disable your old Skyrim Together Reborn mod and the old SkyParkour
   Together mod, then Deploy.
4. Start the game with Data\SkyrimTogetherReborn\SkyrimTogether.exe (add it
   as a tool in Vortex's dashboard if it is not there).


HOSTING THE SERVER
------------------
Run SkyrimTogetherServer.exe from this mod's SkyrimTogetherReborn folder.
Its settings are in config\STServer.ini next to it. Players connect to your
IP (or 127.0.0.1 on the same PC).


NUCLEUS CO-OP (SPLIT SCREEN)
----------------------------
Nucleus starts SkyrimTogether.exe and the server from the folder you gave it.
After installing, open Nucleus, remove and re-add the Skyrim Together Reborn
game, and point it at this mod's SkyrimTogetherReborn\SkyrimTogether.exe.
Keep SmoothCam disabled for split screen.


CHECK THAT IT WORKS
-------------------
SkyrimTogetherReborn\logs\tp_client.log (MO2: the overwrite folder) should show
lines starting with:
  "mod sync:"        parkour sync is running
  "RaceMenu sync:"   appearance sync is running
  "Ragdoll sync:"    ragdoll sync is running
Documents\My Games\Skyrim Special Edition\SKSE\SkyParkourTogether.log should say
"Skyrim Together add-on found".


UNINSTALL
---------
Disable or remove this mod, enable your old Skyrim Together Reborn mod again
and point the SkyrimTogether executable back at it.
