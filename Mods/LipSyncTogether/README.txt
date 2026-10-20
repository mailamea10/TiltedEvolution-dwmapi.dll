LipSync Together 0.8.0
Voice lip sync and proximity voice chat for Skyrim Together Reborn

Talk through your character. LipSync Together moves your mouth while you speak and shows other players' mouth shapes on their screens. Voice chat is optional; when on, your voice travels to the server host's small UDP relay and nearby players hear it through their characters.

FEATURES
- Speech detection ignores most background noise and drives Skyrim's jaw, lip and teeth shapes.
- Optional proximity voice chat, with automatic yell and whisper range. Audio is Opus-encoded and sent only while the voice gate is open.
- The in-game settings window opens with Shift+V. Changes take effect immediately; Save writes them to LipSyncTogether.ini.
- One SKSE plugin DLL supports all included Skyrim runtimes.
- Compatible with the official STR 1.8.3, STR 1.8.5 and the STR 1.8.6 playtest. Install the exact matching game and STR version selected by the FOMOD.

REQUIREMENTS
Every player who wants to use lip sync needs this mod, Skyrim Together Reborn, SKSE for their game version, and Mfg Fix NG or Mfg Fix for their game version. Mfg Fix files are not included. Every player who wants to send or hear voice also needs voice chat enabled in this mod. No server changes are required.

FOMOD OPTIONS
Choose your Skyrim Together version and Skyrim runtime. Then choose voice relay access:
- Secure (recommended): the relay accepts only keys relayed through this Skyrim Together server. The server retains those keys until it restarts, so former players can still connect until then.
- Open (as tested): anyone who can reach the host's relay port can join and hear voice chat.

The key limits who may join; it does not encrypt audio. Voice traffic is not encrypted, recorded or stored. Use a trusted network if protection from traffic interception matters.

VOICE CHAT SETUP
Voice chat and lip sync are on by default. The host's game runs the relay automatically when it detects that the Skyrim Together server is on the same PC. Everyone else connects to that host. The relay uses UDP port 10579; allow it through the host firewall and forward it on the router when players connect over the internet. As with Skyrim Together, each player must use the same STR version and server.

If the host joined its own server by its public internet address, set HostVoiceRelay=On in LipSyncTogether.ini. If you want only lip sync and use Discord or another voice app, set VoiceChat=0. The Secure/Open choice can also be changed with RequireVoiceKey in the settings window or INI.

INSTALLATION
The shared Skyrim Together launcher must sit next to the rest of Skyrim Together.

Mod Organizer 2: Install this mod, then open its SkyrimTogetherReborn folder and your Skyrim Together Reborn mod's SkyrimTogetherReborn folder. Back up the existing SkyrimTogether.exe as SkyrimTogether.original.exe, then copy this mod's launcher into the STR folder. For Skyrim 1.5.97, also copy EarlyLoad.dll, TPProcess.exe and versionlib-ae-to-se-1-5-97-0.map. Keep this mod enabled for its SKSE plugin and start through your usual MO2 Skyrim Together entry; do not launch the exe from this mod's own folder.

Vortex: Install after Skyrim Together Reborn and deploy.

MICROPHONE AND TROUBLESHOOTING
The default Windows communications microphone is used. If your mouth does not move, check Documents\My Games\Skyrim Special Edition\SKSE\LipSyncTogether.log for the device name and set Device= in SKSE\Plugins\LipSyncTogether.ini.

If the launcher closes immediately, it was started outside the Skyrim Together folder. If the log says Mfg Fix was not found, install Mfg Fix NG or Mfg Fix for your runtime. If voice chat does not connect, check HostVoiceRelay, the server host address and UDP 10579 firewall/router rules. Players outside the STR session cannot use the Secure relay option.

COMPATIBILITY
SkyParkour Together and RaceMenu Together ship the same shared launcher with every add-on; these mods can be installed in any order. Other mods that replace SkyrimTogether.exe conflict. Do not combine with another lip sync mod that writes the same mouth phonemes.

PRIVACY
The microphone is opened while the game is running with the LipSync Together add-on. Audio is analysed locally. In voice-chat mode, Opus audio is sent over UDP to the relay and forwarded to the other players; it is not recorded or stored. Secure mode restricts relay admission to players whose session key reached the host through Skyrim Together. The session key does not encrypt the audio packets.

UNINSTALL
MO2: remove the copied launcher from the Skyrim Together Reborn mod, restore SkyrimTogether.original.exe, and remove this mod. Vortex: remove the mod and redeploy.

SOURCE AND CREDITS
GPLv3 source: https://github.com/mailamea10/TiltedEvolution-dwmapi.dll/tree/release/lipsync-together/Mods/LipSyncTogether
Thanks to the Mfg Fix NG and Mfg Fix authors, Tilted Phoques and Skyrim Together Reborn contributors, and absol89. This is an unofficial add-on; no Mfg Fix files are included.