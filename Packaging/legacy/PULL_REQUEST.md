# Skyrim SE 1.5.97 support for STR 1.8.5

This PR lets Skyrim Together Reborn 1.8.5 run on Skyrim SE 1.5.97 and play on the regular 1.8.5 server, with every 1.8.5 feature available.

## Summary

The 1.5.97 build is the 1.8.5 client compiled a second time with `SKYRIM_TARGET_LEGACY`, plus a translation map from the AE address-library ids 1.8.5 uses to their 1.5.97 addresses. Nothing changes for 1.6 and 1.7 players: every 1.5.97 difference sits behind that define or behind a check for the 1.5.97 executable.

- **Same server, same protocol.** It reports v1.8.5 and joins any regular STR 1.8.5 server, so 1.5.97 and 1.6/1.7 players play together.
- **Same features.** Every function the 1.8.5 client calls has a 1.5.97 address. The 3,709-entry map has no unmapped id left that the client uses.
- **Same launch.** It starts from the immersive launcher (`SkyrimTogether.exe`) like the official build, and writes nothing to the game folder.
- **New targets.** `SkyrimTogetherClientLegacy`, `SkyrimImmersiveLauncherLegacy` (with console) and `SkyrimImmersiveLauncherLegacyNoConsole`, plus a CI workflow that builds and packages them.

## How it started

1. **SKSE Menu Framework compatibility (shipped in 1.8.5, [absol89 #12](https://github.com/absol89/TiltedEvolutionScriptFixes/pull/12)).** The launcher adds the Steam folder to the DLL search path, and Steam's own `dwmapi.dll` shadowed the System32 copy. SKSE Menu Framework mods (Show Player in Inventory, Immersive First Person View) and Fluid Motion then failed under the launcher. The launcher now loads the System32 `dwmapi.dll` before anything else. ENB and ReShade proxies (`d3d11`, `dxgi`, `dinput8`) are left alone on purpose.
2. **First 1.5.97 port.** The address-library translation, `GamePatch` helpers and SKSE bootstrap from [Ethanfate1/SkyrimTogetherNext](https://github.com/Ethanfate1/SkyrimTogetherNext) were brought in, with 1.5.x struct layouts under `SKYRIM_TARGET_LEGACY` and a CI build.
3. **Rebased onto the v1.8.5 release.** The first port sat on an older snapshot whose protocol did not match the released 1.8.5 server: players joined with empty names and never saw each other. Rebuilding on the v1.8.5 tag fixed that.
4. **Sync on large load orders.** Plugin lists, magic, projectiles and concentration spells were fixed for 1.5.97 and for 3,000+ plugin setups such as Nolvus (details below).
5. **Immersive launcher on 1.5.97.** The launcher was built against the 1.5.x client, then made to load SKSE plugins such as CBPC and JContainers correctly.
6. **Tween menu camera.** The camera no longer drifts while the Tween menu is open and the game is unpaused.
7. **Full 1.8.5 parity.** Every remaining 1.8.5 function and patch was mapped or ported to 1.5.97.
8. **Release packaging.** The id map ships next to the launcher, nothing is written to the game folder, and the launcher comes with and without a console.

## Every 1.8.5 feature on 1.5.97

"Same code" means the 1.8.5 source compiles unchanged and finds its game functions through the id map; "Ported" names what 1.5.97 needed on top. Party nametags and hostile mode were separate absol89 builds and are not part of 1.8.5.

| Feature | Added in | On 1.5.97 |
| --- | --- | --- |
| Adaptive playback for remote players (replaces the fixed 300 ms delay; resets on ownership change, relays sooner) | 1.8.5 | Same code |
| Nagle disabled on the connection (TiltedConnect) | 1.8.5 | Same code |
| Crash fix: spawning a remote actor that is still loading | 1.8.5 | Same code |
| Crash fix: Havok crash when an actor re-rolls into another creature (#917) | 1.8.5 | Same code |
| Animation graph manager released once when loading variables | 1.8.5 | Same code |
| KiLoader plugins find their DLL dependencies | 1.8.5 | Same code |
| Overlay UI process not de-elevated (TiltedUI) | 1.8.5 | Same code |
| SKSE Menu Framework mods and Fluid Motion work under the launcher (System32 dwmapi.dll, absol89 #12) | 1.8.5 | Same code |
| Startup movie skipped | 1.8.5 | Ported: the sIntroMovie branch at RVA 0x5B52BA |
| Resizable window in windowed mode | 1.8.5 | Ported: window style at RVA 0xD71FAA |
| Named tasklet threads in crash logs | 1.8.5 | Ported: the BSTaskletManager constructor call at RVA 0xC399A6 |
| First-person check for sync | 1.8.5 | Ported: compares the camera state's vtable via RTTI |
| Player-to-player trading, with docked UI, category filters and search | 1.8.4 | Same code |
| Trading crafted potions and poisons, and poisoned or enchanted gear | 1.8.4 | Ported: AddPotion and AddPoison mapped |
| Dropped items synced between players (#913) | 1.8.4 | Same code |
| Mod animation events synced, /wave while sheathed (absol89 #2) | 1.8.4 | Same code |
| KiLoader support for ENB | 1.8.4 | Same code; ENB and KiLoader proxies also kept out of the overlay helpers |
| Torso-less remote player bodies restored | 1.8.4 | Same code |
| Beast form revert race fix (#914) | 1.8.4 | Same code |
| Dialogue unstuck helper (#916) | 1.8.4 | Same code |
| NPC rapid duplication fix (#906) | 1.8.3 | Same code |
| Objects and NPCs no longer glide after collisions (Havok timestep, #901) | 1.8.3 | Ported: AIProcess::GetCharController and the physics timestep mapped |
| Party hint when questing solo next to others (#909) | 1.8.3 | Same code |
| Spell cast target id fix (#907) | 1.8.3 | Same code |
| Respawn fix for DLC1DimHollowCrypt02 (#902) | 1.8.3 | Same code |
| STServer.ini comments and `--generate-config` | 1.8.3 | Server side |
| Swedish translation (#910) | 1.8.3 | Same code |
| Leveled NPC sync: first pick wins, base rebuilt from the original NPC (#892, #899) | 1.8.3 | Ported: SetLeveledCreature, GarbageCollector::Add and its singleton mapped |
| Versioned ownership grants from the server (#887) | 1.8.3 | Same code |
| Dialogue sync when the speaker does not own the NPC (#896) | 1.8.3 | Same code |
| Party invitations can be retried and expire consistently (#368) | 1.8.3 | Same code |
| Shared time advances when a player sleeps or waits (#467) | 1.8.3 | Same code |
| Weapon state restored when a forced draw or sheathe is refused (#810) | 1.8.3 | Same code |
| Staff summons skip spell cast sync (#894) | 1.8.3 | Same code |
| Jail containers and player-home containers kept out of object sync (#893) | 1.8.3 | Ported: the cell's loaded data modelled at +0x128 |
| Dragons spawn for party members; pickpocketing updates inventories | 1.8.3 | Same code |
| Camera no longer stuck after respawn | 1.8.3 | Same code |
| "Incompatible game version" popup | 1.8.3 | Same code |
| Companions "Brotherhood" induction no longer hangs (quest patch esp, #855) | 1.8.3 | Same plugin |
| Clear stuck screen effects (F3 menu) | 1.8.1 | Same code |
| Animation events from console commands and Immersive Interactions | 1.8.1 | Same code |
| Respawn overrides for problem interiors (#875) | 1.8.1 | Same code |
| Enemy-equipped weapon handling, slow-time caster id fix | 1.8.1 | Same code |
| Quest and scene handling fixes | 1.8.1 | Same code |
| Naked NPC fixes, two-stage re-dressing, limbs drawn without a chest slot | 1.8.1 | Same code |
| Hide password, hide location in the player list (#873, #868) | 1.8.1 | Same code |
| Private servers auto-join, party leaders use /settime, no auto-join on public servers (#877, #874) | 1.8.1 | Same code |
| Animation replay resets the animation graph | 1.8.x | Ported: RevertAnimationGraphManager mapped |
| Close all menus (debug tools) | 1.8.x | Ported: UI::CloseAllMenus mapped |

## 1.5.97-specific work

**Addresses and layouts**

- `versionlib-ae-to-se-1-5-97-0.map` translates the AE ids 1.8.5 uses into 1.5.97 offsets of the SE Address Library (3,709 entries). Ids that no automatic mapping covered were found by disassembly and added as `# manual:` entries with the evidence in the comment.
- An id with no entry resolves to a zero-returning stub and logs a warning, so a gap degrades a feature instead of crashing. Data ids (singletons, globals) never go through the stub.
- Struct layouts that differ on 1.5.x sit under `SKYRIM_TARGET_LEGACY`: `TESObjectREFR` is 8 bytes smaller, so `Projectile` fields are laid out relative to it. Also `PlayerCharacter`, `TESObjectCELL` (references, loaded data), `ExtraDataList`, `BSAnimationGraphManager`, `SkyrimVM` (inactive flag at 0x680).
- `GamePatch::AtLegacyRva` patches a 1.5.97 site only on that exact executable and only while its bytes are unchanged, so other 1.5.x builds and sites already patched by mods are skipped and logged.
- `RttiScan::FindVTable` finds a vtable by its RTTI type name, which needs no address id at all.

**Sync fixes found on 1.5.97 with large load orders**

- The plugin list is built from the engine's compiled load order. On Nolvus (about 3,700 plugins) the raw file list missed real plugins, so their forms never crossed the wire.
- The outgoing message buffer grew from 64 KiB to 1 MiB. The authentication request carries every plugin name and was cut off past 64 KiB.
- Spell cast hooks are verified against the live `ActorMagicCaster` vtable, so concentration spells (Flames, Sparks, Frostbite) sync.
- Papyrus natives are captured after the game binds its own, which fixed a crash on door activation.

**Immersive launcher on 1.5.97**

- The 1.5.97 `SkyrimSE.exe` is SteamStub-wrapped; the launcher's existing unwrapper handles it. The game path is stored under its own registry key, so the 1.5.97 and 1.6 launchers do not overwrite each other.
- SKSE starts after the game's static initialization, as `skse64_loader` does. Starting it earlier crashed CBPC.
- The low 2 GB of address space are reserved at startup and handed back on demand. LuaJIT plugins built without GC64 (JContainers 4.1.x) need their Lua heap there; without it JContainers crashed on the first save load.
- `World::Update` runs on the game's main thread. On 1.5.97 the `SkyrimVM::Update` tick runs on another thread, so a tick there only requests the update and the next `Main::Update` runs it. A window timer remains as a fallback.
- The Tween menu camera is held in place while connected. STR keeps the game running under that menu, and camera mods that add an offset every frame (Camera Noise) made the view drift through walls.
- The SKSE bootstrap plugin stands down inside the launcher, so a leftover copy cannot start a second client or copy files into the game folder.
- `TPProcess.exe` pins the System32 `d3d11`/`dxgi` in the overlay helpers, so ENB and KiLoader proxies stay out of them.
- The crash handler logs the faulting module and offset, the access type and a stack scan. Dumps go to the launcher's `logs` folder; only `crash_*.dmp` files are ever pruned.

## Release package, install and build

| Download | Launcher |
| --- | --- |
| `SkyrimTogetherReborn-1.8.5-SE-1.5.97.zip` | No console window |
| `SkyrimTogetherReborn-1.8.5-SE-1.5.97-console.zip` | Console window with the live log, for troubleshooting |

Each zip holds `SkyrimTogetherReborn/SkyrimTogether.exe`, `EarlyLoad.dll`, `TPProcess.exe`, `versionlib-ae-to-se-1-5-97-0.map`, a README and the GPL licence. Players extract it over the STR 1.8.5 mod and launch `SkyrimTogetherReborn/SkyrimTogether.exe` from their mod manager. The map is read from next to the launcher first and from `Data/SKSE/Plugins` as a fallback, so the only extra requirement is the SE Address Library (`version-1-5-97-0.bin`).

Build: `xmake build SkyrimImmersiveLauncherLegacy SkyrimImmersiveLauncherLegacyNoConsole`. The `Legacy 1.5.97 build` workflow (`.github/workflows/legacy-build.yml`) builds the client, both launchers, `TPProcess` and the SKSE bootstrap, and packages both zips. It tags the build with the server version it talks to (v1.8.5), because the server rejects a client whose version string differs.

## Testing

Tested on Nolvus v6 (SkyrimSE 1.5.97, SKSE 2.0.20, about 3,700 plugins) against a stock STR 1.8.5 server, with two game instances on one PC.

- [x] Launcher boots 1.5.97, starts SKSE, loads a save, connects and joins in about 0.2 s
- [x] No unmapped-id warnings in the log; startup movie skipped; resizable window
- [x] Two players see each other with mod-added races, faces and gear
- [x] Spells, concentration spells and magic projectiles sync
- [x] Tween menu camera stays put with Camera Noise, SmoothCam and True Directional Movement installed
- [x] CBPC and JContainers load under the launcher
- [x] Nothing new in the game folder after a session; logs and dumps land beside the launcher (MO2 overwrite)
- [ ] Two-player pass on the final build: trading (potions, poisons, enchanted gear), dropped items, PvP, party and quests
- [ ] SKSE Menu Framework mods (Show Player in Inventory, Immersive First Person View) and Fluid Motion on 1.5.97
- [ ] KiLoader with ENB under the 1.5.97 launcher
- [ ] Horses, beast forms, leveled NPC encounters

## Credits and license

- [Tilted Phoques](https://github.com/tiltedphoques/TiltedEvolution) and the Skyrim Together Reborn team, for Skyrim Together Reborn
- [absol89](https://github.com/absol89/TiltedEvolutionScriptFixes), for the 1.8.5 release this is built on
- [Ethanfate1 / SkyrimTogetherNext](https://github.com/Ethanfate1/SkyrimTogetherNext), for the original 1.5.97 groundwork (address translation, GamePatch, SKSE bootstrap)
- [miredirex](https://github.com/miredirex/SkyrimTogether), MostExcellent, DJLegends1011 and GhostWriter42, for 1.8.x features this build carries
- mailamea10, for the SKSE Menu Framework fix and the 1.5.97 port

Skyrim Together Reborn is licensed under the GNU General Public License v3; this build is too.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01XJRunXpCd16whPW6bmQ4Z7
