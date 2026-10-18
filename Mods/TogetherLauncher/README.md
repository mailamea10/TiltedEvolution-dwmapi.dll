# Together Launcher

The Skyrim Together launcher (`SkyrimTogether.exe`) that every one of our SKSE mod releases ships, built
once per supported Skyrim Together release and game version with **all** of our Skyrim Together add-ons in it.

Each mod (SkyParkour Together, RaceMenu Together, LipSync Together, ...) has two halves: an SKSE plugin and a small add-on
inside the Skyrim Together launcher. Only one `SkyrimTogether.exe` can be in use, so if every mod shipped a
launcher with only its own add-on, installing two of them would switch one off. With one shared launcher:

- every mod's archive carries the identical `SkyrimTogether.exe`, so mod order and install order do not matter;
- an add-on does nothing until its own SKSE plugin is installed, so players who only use one mod are unaffected;
- no add-on changes the server; they all use the script animation relay every stock server has.

## Targets

| Folder | Skyrim Together | Skyrim | Base commit |
|---|---|---|---|
| `10 STR 1.8.3 - 1.7.104` | official 1.8.3 (Nexus main file) | 1.7.104 | `4917189f` |
| `11 STR 1.8.3 - 1.6.1170` | official 1.8.3 (Nexus optional file) | 1.6.1170 / 1.6.1179 | `db8af2cf` |
| `20 STR 1.8.5 - 1.7.104` | 1.8.5 (absol89) | 1.7.104 | `0f1876e6` |
| `21 STR 1.8.5 - 1.6.1170` | 1.8.5 (absol89) | 1.6.1170 / 1.6.1179 | `043dad02` |
| `22 STR 1.8.5 - 1.5.97` | 1.8.5, 1.5.97 port | 1.5.97 | `e6a097e1` |

## Build

`.github/workflows/together-launcher-release.yml` checks out each base, applies every add-on listed in
`addons.txt` (each from the head of its own release branch), builds the launcher, and publishes one archive
per target to the `together-launcher` pre-release, with `LAUNCHER_MANIFEST.txt` recording the base and
add-on commits. It then starts every mod's release workflow, which packages its FOMOD with these launchers.

## Adding a new mod

1. Give the mod a release branch with its add-on under `Mods/<Mod>/StrAddon/apply_addon.py`
   (`apply_addon.py <STR tree> --protocol 1.8.5|1.8.3`; it copies its sources and registers its service in
   `World.cpp`), and a release workflow that downloads the launchers from `together-launcher` (see the
   SkyParkour Together or RaceMenu Together workflow).
2. Add a line for it to `addons.txt` and push this branch. The launcher is rebuilt and every mod's release
   is repackaged with it.

Add-ons must stay independent: their own service, their own wire prefix on the script animation relay, and an
export the plugin can find (`STRModSync_HostApiVersion`, `STRRaceMenu_HostApiVersion`, ...).
