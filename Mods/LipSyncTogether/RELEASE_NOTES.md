# LipSync Together 0.8.0

LipSync Together now includes optional proximity voice chat alongside the mouth animation you tested. Voice is encoded as Opus and sent directly over UDP to the relay hosted by the Skyrim Together server host. It is not recorded or stored.

The FOMOD offers two relay access modes:
- **Secure (recommended):** each player has a random session key that is relayed through Skyrim Together; the relay admits clients whose key was relayed through this Skyrim Together server. The server retains keys until it restarts.
- **Open (as tested):** anyone who can reach the relay's UDP port may join and hear the voice chat.

The session key controls who may join; it does not encrypt the audio. Use a trusted network if you need protection from traffic interception. Voice chat is on by default. The host must allow or forward UDP port 10579 as needed. The in-game settings window opens with Shift+V.

The plugin remains one SKSE DLL for every supported runtime. The FOMOD includes the official Skyrim Together Reborn 1.8.3, the 1.8.5 release and 1.8.6 playtest targets, across all supported Skyrim versions.

Requires SKSE and Mfg Fix NG or Mfg Fix for the game version. No files from those mods are included.