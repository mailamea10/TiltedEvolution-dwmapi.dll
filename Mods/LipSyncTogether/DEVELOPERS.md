# LipSync Together: building and combining

Everything needed to put LipSync Together into a Skyrim Together tree, alone or next to our other add-ons.

## Layout

| Path | What |
|---|---|
| `Plugin/` | SKSE plugin (`build.bat` from an x64 VS prompt; no dependencies), default `LipSyncTogether.ini` |
| `StrAddon/Code/client/` | add-on sources copied into the STR tree |
| `StrAddon/apply_addon.py` | applies the add-on to an STR tree |
| `fomod/`, `README.txt`, `RELEASE_NOTES.md`, `Nexus/` | packaging and docs |
| `.github/workflows/lipsync-together-release.yml` | builds the plugin, downloads the shared launchers, publishes the FOMOD |

## Applying the add-on

```
python Mods/LipSyncTogether/StrAddon/apply_addon.py <STR tree> [--protocol 1.8.5|1.8.3]
```

It is idempotent and only touches:
- `Code/client/World.cpp`: include and `ctx().emplace<LipSyncTogetherService>(...)` after `MapService`.
- `Code/client/Games/Misc/BSScript.cpp`: include `LipSyncTogether/LipSyncNatives.h` and call
  `LipSyncTogether_OnNativeRegistered(apFunction);` just before the hook chains to `RealRegisterPapyrusFunction`.
  Mfg Fix NG registers its natives with CommonLib, which keeps the callback in a `std::function`, so the add-on needs
  the function object itself, not the address `PapyrusService` records. The add-on checks the MSVC `std::function`
  layout and the RTTI signature before calling anything.
- `--protocol 1.8.3` switches the relay's object id to 0 (1.8.3 skips id 0; 1.8.5 resolves a server id, so
  `0xFFFFFFFF` is used).

Both anchors exist on every launcher base (`4917189f`, `db8af2cf`, `0f1876e6`, `043dad02`, `e6a097e1`) and on the
legacy feature branches. Applying it next to SkyParkour Together and RaceMenu Together in any order is fine.

## Independence from the other add-ons

- Own service (`LipSyncTogetherService`), own wire prefix `STRLS1|` on the script animation relay.
- Exports `STRLipSync_HostApiVersion` from the launcher; the plugin exports `STRLipSync_GetDescriptor`
  (API version 2, `LipSyncTogether/STRLipSyncAPI.h`).
- No server change. Messages are one of 104 fixed mouth states per player, because the server's string cache
  keeps every distinct relayed string until it restarts.

## Shared launcher

LipSync Together is listed in `Mods/TogetherLauncher/addons.txt` on `release/together-launcher`. Pushing that
branch rebuilds the five launchers with every listed add-on and then runs every mod's release workflow, this one
included. This workflow refuses to package a launcher whose manifest does not list `release/lipsync-together`.

## Local test build

On a feature/test branch: `python <this branch>/Mods/LipSyncTogether/StrAddon/apply_addon.py .`, then
`.\Build-STR.ps1`, and copy `Plugin/build/LipSyncTogether.dll` and `LipSyncTogether.ini` into the mod's
`SKSE/Plugins`.
