TiltedEvolution - RaceMenu + Parkour (Skyrim Special Edition 1.5.97)
====================================================================

This is a complete mod-manager package for the combined legacy 1.5.97 client.
It includes the Skyrim Together data files and matching legacy runtime; it is
not an overlay that needs the official Skyrim Together mod installed first.

REQUIREMENTS
------------
- Skyrim Special Edition 1.5.97.0 (SkyrimSE.exe)
- SKSE64 2.0.20
- Address Library for SKSE Plugins - All in One (SE)
- RaceMenu for Skyrim SE 1.5.97
- The matching server archive published alongside this client. The server must
  relay the RaceMenu appearance message used by this build.

INSTALL WITH MOD ORGANIZER 2 OR VORTEX
-------------------------------------
1. Install this ZIP as a mod. The FOMOD installer installs the core files and
   lets you choose the standard launcher or the console launcher.
2. Ensure RaceMenu is enabled and set bPlayerOnly=0 in skee64.ini.
3. Add SkyrimTogetherRuntime\SkyrimTogether.exe as a mod-manager executable if
   it is not added automatically.
4. Launch the game through your mod manager. Every player should use this same
   combined client build and the compatible server.

TWO-INSTANCE TESTING
--------------------
If you use the existing kiLoader workaround, start the first instance with
SkyrimTogetherRuntime\SkyrimTogether.exe and start the second instance through
SKSE from the mod manager. Do not change the Parkour files to troubleshoot
RaceMenu replication; Parkour sync is a separate feature.

PARKOUR
-------
This package synchronizes SkyParkour animation events; it does not bundle the
SkyParkour mod itself. Install SkyParkour on any client that will perform its
parkour moves.

MANUAL INSTALL
--------------
Copy the contents of "00 Core" into Skyrim Special Edition\Data, then copy the
chosen launcher from "01 Standard Launcher" or "02 Console Launcher" into the
same Data folder, preserving subfolders. Do not copy the fomod folder into Data.

This is a test/pre-release build. Back up existing mod-manager profiles and
load order before replacing another Skyrim Together installation.
