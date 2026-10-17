Skyrim Together Reborn 1.8.5 for Skyrim Special Edition 1.5.97. Same mod, same features, same server: players on 1.5.97 can now join 1.6 and 1.7 players on any regular STR 1.8.5 server.

## What this is

This is absol89's Skyrim Together Reborn 1.8.5 release, rebuilt for the 1.5.97 game executable. Modlists that stay on 1.5.97 (Nolvus v6, and anything built on SKSE 2.0.20 plugins) no longer have to downgrade or upgrade to play together.

It launches like the official mod: from `SkyrimTogether.exe` in your mod manager. Nothing is copied into your game folder, and nothing is left behind after you quit.

## Requirements

- Skyrim Special Edition **1.5.97** (Steam)
- [SKSE64 2.0.20](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444), the SE (1.5.97) file
- Skyrim Together Reborn **1.8.5**, installed as a mod (the plugins, scripts, behaviours and the `SkyrimTogetherReborn` folder)
- A server running STR 1.8.5 (any regular 1.8.5 server works)

## Downloads

- **SkyrimTogetherReborn-1.8.5-SE-1.5.97** (main file): the launcher without a console window.
- **SkyrimTogetherReborn-1.8.5-SE-1.5.97-console** (optional): the same launcher with a console window that shows the log as you play. Use it when reporting a problem.

Install one of the two; everything else in them is the same.

## Installation

1. Install Skyrim Together Reborn 1.8.5 in your mod manager as usual.
2. Install this file as a mod placed **after** it (or merge it into the same mod), so its files win the conflicts. It replaces `SkyrimTogetherReborn/SkyrimTogether.exe`, `EarlyLoad.dll` and `TPProcess.exe`, and adds `versionlib-ae-to-se-1-5-97-0.map`.
3. In Mod Organizer 2, add `SkyrimTogetherReborn/SkyrimTogether.exe` (inside the mod folder) as an executable and launch it from MO2. Vortex users: launch it as a tool.
4. On the first start the launcher asks for your game folder: pick the folder with `SkyrimSE.exe` (for Nolvus, `STOCK GAME`). It remembers it separately from the 1.6 launcher.
5. In game, press **F2** to open the overlay and connect.

If an older test build put `SkyrimTogetherSKSE.dll` in `Data/SKSE/Plugins` or `SkyrimTogetherRuntime_1_5.dll` in your game folder, you can delete them; this release does not use them.

## Features

Everything in Skyrim Together Reborn 1.8.5 works on 1.5.97.

### Mod compatibility (where this all started)

- **SKSE Menu Framework mods work under the launcher.** Show Player in Inventory, Immersive First Person View and other menu-framework mods, plus Fluid Motion, no longer fail when the game is started through Skyrim Together. The cause was an old `dwmapi.dll` in the Steam folder being picked over Windows' own; the launcher now always uses the Windows copy. This fix shipped in 1.8.5 and was the first step toward this port.
- **KiLoader and ENB.** KiLoader plugins load under the launcher and find their DLLs, and ENB/KiLoader no longer load into the overlay's helper processes.
- **Big load orders.** Tested with Nolvus v6 (about 3,700 plugins): every plugin is reported to the server, so spells, races and gear from mods sync correctly.
- **SKSE plugins that need special care**: CBPC and JContainers (and other LuaJIT-based plugins) load and run under the launcher.
- **Camera mods.** The Tween menu camera stays in place even with camera mods such as Camera Noise, SmoothCam and True Directional Movement.

### Smoother, more responsive play (1.8.5)

- Adaptive interpolation replaces the fixed 300 ms delay on other players, so movement and PvP feel more responsive, especially on LAN.
- Lower network latency (Nagle disabled) and quicker relaying of movement.
- Animation fixes from MostExcellent and miredirex.

### Stability (1.8.5 and earlier)

- Crash fixes: remote actors that are still loading, creatures that re-roll into another creature, animation variable loading.
- No more rapid NPC duplication in world encounters.
- Objects and NPCs no longer glide after collisions (Havok timing fix).
- Camera no longer gets stuck after a respawn.
- Respawn overrides for interiors that are bad places to die, including DLC1DimHollowCrypt02.
- "Clear stuck screen effects" in the F3 menu, and a dialogue unstuck helper.

### Trading and items (1.8.4)

- **Player-to-player trading** from the F2 overlay, docked beside chat, with Skyrim item categories, search and quantity controls. Quest items are excluded.
- Trade **crafted potions and poisons**, and **poisoned or enchanted weapons and armour**; they arrive exactly as crafted.
- **Dropped items sync** between players.

### Characters and animation

- Remote players' bodies show modded armour that has no torso (torso-less bodies restored).
- Mod animation events sync, and `/wave` works while sheathed.
- Animation events sent from console commands or Immersive Interactions sync.
- Beast form revert fix (werewolf and vampire lord).
- Naked-NPC fixes: NPCs keep their outfits, with two-stage re-dressing, and limbs render without a chest piece.
- Leveled NPCs and creatures are the same for everyone: the first player's pick wins and the NPC keeps its name and outfit.

### Party, quests and world

- Ownership of NPCs and objects is granted by the server with versions, which stops fights over who controls an NPC.
- Dialogue syncs even when the speaker is controlled by another player.
- Party invitations can be retried and expire consistently; a hint suggests forming a party when you quest next to others.
- Time advances for everyone when a player sleeps or waits.
- Dragons spawn for party members; pickpocketing updates inventories.
- Jail and player-home containers stay private to each player.
- Companions "The Brotherhood" induction no longer hangs (quest patch plugin).
- Quest and scene handling fixes.

### Servers and UI

- Private servers auto-join the party; public servers never do. Party leaders can use `/settime` on private servers.
- Hide password in the connect dialog; locations hidden in the player list.
- "Incompatible game version" popup when client and server do not match.
- Swedish translation.
- Startup movie skipped, and the game window is resizable in windowed mode.

### Made for 1.5.97

- Works with the 1.5.97 `SkyrimSE.exe` and SKSE 2.0.20; joins regular 1.8.5 servers.
- Nothing is written to your game folder. Logs, and a crash dump if the game crashes, go to `SkyrimTogetherReborn/logs` (with MO2, in your overwrite folder).
- The crash log names the module and offset that crashed, so reports are useful without a multi-gigabyte dump.
- The 1.5.97 and 1.6 launchers remember their game folders separately, so you can keep both installed.

## FAQ

**Can I play with friends on 1.6 or 1.7?** Yes. Everyone needs STR 1.8.5 (this file on 1.5.97, the regular release on 1.6/1.7) and the same server.

**Do I still need the Address Library?** Yes, the SE (1.5.97) file. This mod ships only the small map that translates the 1.8.5 addresses to it.

**The game does not start / crashes.** Use the console download and send `SkyrimTogetherReborn/logs/tp_client.log` (in MO2, check overwrite) with your report.

**Does it work on 1.5.80 or other SE builds?** No, only 1.5.97.

## Credits

- Tilted Phoques and the Skyrim Together Reborn team
- absol89, for the 1.8.5 release this is built on
- Ethanfate1 (SkyrimTogetherNext), for the original 1.5.97 groundwork
- miredirex, MostExcellent, DJLegends1011 and GhostWriter42, for 1.8.x features
- mailamea10, for the SKSE Menu Framework fix and this 1.5.97 port

## Permissions and source

Skyrim Together Reborn is open source under the GNU GPL v3, and so is this build. Source code: [github.com/mailamea10/TiltedEvolution-dwmapi.dll](https://github.com/mailamea10/TiltedEvolution-dwmapi.dll) (branch `feature/legacy-1.5.97-on-1.8.5`).
