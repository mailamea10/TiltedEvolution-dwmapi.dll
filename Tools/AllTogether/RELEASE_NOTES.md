**AllTogether SKSE 2** bundles Ragdoll/Physics Together, LipSync Together, RaceMenu Together and SkyParkour Together with one Skyrim Together launcher, in one FOMOD.

**Skyrim Together:** the official Skyrim Together Reborn 1.8.4 from [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/69993), built from Tilted Phoques' v1.8.4 source.
**Skyrim:** 1.7.104 and 1.6.1170/1.6.1179.

**What changed since bundle 1**
- The add-ons no longer send their data as script animation names, which the server keeps for the whole session (the cause of servers filling up and late joiners failing, issue #2). They use Skyrim Together's Plugin API: on a server with version 2, messages go only to the players near that character or NPC and nothing is stored.
- New optional **AllTogether server** (FOMOD step "Server"), for hosts. Ragdoll and RaceMenu sync need it; players without the add-ons can still join it.
- Ragdolls stay on for every NPC, player and remote, with less work per frame.
- RaceMenu Together no longer uses a Windows compression API that Wine/Proton lack (likely cause of the Linux crash, issue #1).
- LipSync voice keys are sent again every 20 seconds instead of relying on the server cache.

See the installed Docs for setup, including the MO2 step of copying the launcher into your Skyrim Together folder.
