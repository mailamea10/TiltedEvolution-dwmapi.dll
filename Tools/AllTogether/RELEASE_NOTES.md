**AllTogether SKSE 2** bundles Ragdoll/Physics Together, LipSync Together, RaceMenu Together and SkyParkour Together with one Skyrim Together launcher for every supported version, in one FOMOD.

**Skyrim Together:** 1.8.3 (Nexus), 1.8.4, 1.8.5 and 1.8.7-4 (absol89).
**Skyrim:** 1.7.104, 1.6.1170/1.6.1179, and 1.5.97 on STR 1.8.5 and 1.8.7-4 (with KiLoader support).

**What changed since bundle 1**
- The add-ons no longer send their data as script animation names, which the server keeps for the whole session (the cause of servers filling up and late joiners failing, issue #2). They use Skyrim Together's Plugin API: on a server with version 2, messages go only to the players near that character or NPC and nothing is stored.
- New optional **AllTogether server** for each Skyrim Together version (FOMOD step "Server"), for hosts. Ragdoll and RaceMenu sync need it; players without the add-ons can still join it.
- Ragdolls stay on for every NPC, player and remote, with less work per frame.
- RaceMenu Together no longer uses a Windows compression API that Wine/Proton lack (likely cause of the Linux crash, issue #1).
- LipSync voice keys are sent again every 20 seconds instead of relying on the server cache.
- New targets: STR 1.8.4, STR 1.8.7-4 (absol89's v1.8.7-4 build string, so it joins 1.8.7-4 servers), and 1.5.97 on 1.8.7-4. STR 1.8.6 is dropped.

See the installed Docs for setup, including the MO2 step of copying the launcher into your Skyrim Together folder.
