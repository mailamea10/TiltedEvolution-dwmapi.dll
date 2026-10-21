All four latest released Skyrim Together add-ons, plus combined launchers built from Skyrim Together Reborn 1.8.7.

The FOMOD presents each add-on's banner, a shared Skyrim Together/runtime selection, and LipSync Together's secure/open voice relay choice. It installs the matching launcher files, add-on defaults, and setup instructions with requirement download links.

The launchers include Ragdoll/Physics Together, LipSync Together, RaceMenu Together, and SkyParkour Together integration. Their SKSE plugins remain independently installable and optional.

New STR 1.8.7 launchers are provided for Skyrim 1.7.104 and 1.6.1170/1.6.1179. The add-on archives retain their packages for STR 1.8.3, 1.8.5, and 1.8.6, including legacy Skyrim 1.5.97 support where supplied by the add-on package.

The 1.5.97 launcher is rebuilt with KiLoader dependency search support and includes
EarlyLoad.dll, TPProcess.exe, and the 1.5.97 Address Library map. Add-on files are
installed under SKSE/Plugins, and the selected launchers are installed under
SkyrimTogetherReborn. This is published as a new `-KiLoader-Fix` asset; existing
release assets are retained.

The source and compatibility details are in `SOURCES.txt` and `INSTALLATION.txt`.

The STR 1.8.7 launcher source also includes a RaceMenu Together reconnect fix:
when a player leaves and later reconnects with a reused player ID, the other
clients detect the return and request the existing appearance resend.
