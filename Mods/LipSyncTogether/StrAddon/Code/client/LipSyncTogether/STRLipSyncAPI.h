#pragma once

// STR LipSync API, version 8.
//
// The contract between the LipSync Together SKSE plugin (LipSyncTogether.dll) and the LipSync Together add-on
// built into the Skyrim Together launcher.
//
// The plugin listens to the microphone and turns the voice into a few mouth shapes. It exports one C function,
// STRLipSync_GetDescriptor, returning a descriptor that lives as long as the plugin is loaded. The add-on finds it
// by scanning the loaded modules, so load order does not matter, reads the current mouth shape every frame, puts
// it on the player's face and relays it to the other players. The plugin opens the microphone on the add-on's first
// GetFrame call, so without the add-on the microphone stays closed. The add-on also exports
// STRLipSync_HostApiVersion from the launcher, for tools that want to know whether it is there.
//
// With voice chat on (Settings.VoiceChat), the plugin also carries the voice itself, through a small relay outside
// Skyrim Together: the add-on tells it which server the game joined and the local player id (SetSession), and every
// frame how far away and on which side each other player is (SetMix); the plugin turns that into a volume, using
// the range each speaker's voice carries right now (wider when they yell, narrower when they whisper). Mouths of
// players whose voice is heard come from the received voice as it plays (GetRemoteMouth), so they match what is heard.
//
// The voice relay can be kept to the players of the Skyrim Together server (RequireVoiceKey): every plugin makes a
// random voice key, the add-on relays it to the other players through Skyrim Together (GetVoiceKey) and hands the
// keys it receives back to the plugin (AddVoiceKey). A relay that requires keys lets in only players whose key came
// that way, so someone who is not on the server cannot join the relay and listen.
//
// Every setting of LipSyncTogether.ini is also listed by the plugin (GetSettingCount/GetSettingInfo) and can be read
// and changed while the game runs (GetSetting/SetSetting take effect at once; SaveSettings writes the INI). The
// add-on's in-game settings window is built from that list, so it never needs to know the settings itself.

#include <cstdint>

namespace STRLipSync
{
inline constexpr uint32_t kApiVersion = 8;

inline constexpr char kDescriptorExport[] = "STRLipSync_GetDescriptor";
inline constexpr char kHostApiVersionExport[] = "STRLipSync_HostApiVersion";

// The mouth at one moment. Every shape runs from 0 (not at all) to 1 (fully).
struct Frame
{
    float Open;  // jaw open (loudness)
    float Wide;  // lips spread, as in "ee"
    float Round; // lips rounded, as in "oo"
    float Lips;  // lips pressed together, as in "m" or "b"
    float Teeth; // teeth together, as in "f" or "s"

    uint32_t Speaking; // 1 while the noise gate (and push-to-talk, if set) lets the voice through
    uint32_t Sequence; // goes up with every analysed block of audio, so a stalled microphone can be told apart
};

// Settings from LipSyncTogether.ini that the add-on uses.
struct Settings
{
    uint32_t AnimateSelf;     // 1: the player's own face moves too
    float MouthStrength;      // scales every phoneme the add-on writes (1 = as analysed)
    float RemoteDelaySeconds; // how long a copy holds a received mouth before showing it, to smooth out the network
    float MaxDistance;        // copies further away than this (game units) are left alone
    uint32_t Debug;           // 1: log timing and mouth states (LogLevels in the INI)

    uint32_t VoiceChat;       // 1: carry the voice too (SetSession, SetMix, GetRemoteMouth)
    float NearDistance;       // a normal voice is at full volume up to this distance (game units)
    float FarDistance;        // and fades out to silence at this one
    float MaxRangeScale;      // the furthest a voice can carry (a yell), as a multiple of the distances above

    uint32_t OpenKey;         // virtual-key code that opens the in-game settings window (0 = never)
    uint32_t OpenKeyShift;    // 1: only together with Shift
};

// One setting, as the settings window shows it.
enum SettingType : uint32_t
{
    kSettingBool = 0,         // "0" / "1"
    kSettingInt = 1,          // whole number between Min and Max
    kSettingFloat = 2,        // number between Min and Max
    kSettingKey = 3,          // virtual-key code, "0x56"; "0" for none
    kSettingInputDevice = 4,  // part of a microphone's name; "" for the Windows default
    kSettingOutputDevice = 5, // part of a playback device's name; "" for the Windows default
    kSettingChoice = 6,       // one of Choices (separated by '|')
};

struct SettingInfo
{
    const char* Section; // INI section
    const char* Key;     // INI key
    const char* Label;   // what the settings window calls it
    uint32_t Type;       // SettingType
    float Min;
    float Max;
    const char* Choices; // kSettingChoice only, separated by '|'
    const char* Help;    // one short sentence shown under it

    // Where the settings window shows it: a page ("General", "Voice chat", "Hotkeys", "Advanced") and a card on it,
    // in list order; and the unit its value is shown in ("dB", "ms", "x", "units" - game units, shown in metres -,
    // "bit/s", or "").
    const char* Page;
    const char* Group;
    const char* Unit;
};

// The local microphone right now, for the settings window's meter.
struct Meter
{
    float LevelDb;   // loudness of the last 16 ms
    float NoiseDb;   // background noise the gate is measured against
    float GateDb;    // what counts as loud enough
    float Voicing;   // how clearly pitched (0 to 1); a voice is above VoiceThreshold
    float Open;      // the jaw
    float Range;     // how far the voice carries (1 normal, above yelling, below whispering)
    uint32_t Speaking;
    uint32_t Whispering;
    uint32_t Sequence; // goes up with every block; unchanged means no microphone
    float Calibration; // how much of the speaker's normal voice is learned (1 = done); yell/whisper wait for it
};

// Where one other player is right now, as heard from the local player.
struct MixEntry
{
    uint32_t PlayerId; // Skyrim Together's player id, the same on every client
    float Distance;    // game units between the two characters
    float Pan;         // -1 (left) to 1 (right)
};

struct Descriptor
{
    uint32_t ApiVersion; // kApiVersion
    const char* ModName;
    const char* ModVersion;

    const Settings* Config;

    // Copies the latest mouth into apFrame. Returns 0 when there is nothing yet (no microphone).
    uint32_t (*GetFrame)(Frame* apFrame);

    // Copies the name of the microphone in use (UTF-8, for the log) into apBuffer. Returns 0 while none is open.
    uint32_t (*GetDeviceName)(char* apBuffer, uint32_t aSize);

    // The server the game joined (host name or address, without the port) and the local player id; nullptr when the
    // game left the server. Voice chat starts and stops with it.
    void (*SetSession)(const char* acpServerHost, uint32_t aLocalPlayerId);

    // Every other player within FarDistance * MaxRangeScale; anyone not listed is silent.
    void (*SetMix)(const MixEntry* apEntries, uint32_t aCount);

    // The mouth of a player as their voice plays here. Returns 0 when that player's voice has not been heard for a
    // while; the add-on then uses the relayed mouth states.
    uint32_t (*GetRemoteMouth)(uint32_t aPlayerId, Frame* apFrame);

    // Settings, for the in-game settings window. Every call is safe from any thread.
    uint32_t (*GetSettingCount)();
    const SettingInfo* (*GetSettingInfo)(uint32_t aIndex);
    uint32_t (*GetSetting)(uint32_t aIndex, char* apBuffer, uint32_t aSize); // the value as text
    uint32_t (*SetSetting)(uint32_t aIndex, const char* acpValue);          // takes effect at once; 0 if refused
    uint32_t (*SaveSettings)();                                              // writes LipSyncTogether.ini
    uint32_t (*ReloadSettings)();                                            // reads it again
    uint32_t (*HasUnsavedChanges)();

    // Names of the active microphones (aOutput 0) or playback devices (aOutput 1), one per line.
    uint32_t (*ListDevices)(uint32_t aOutput, char* apBuffer, uint32_t aSize);

    uint32_t (*GetMeter)(Meter* apMeter);
    // A few lines on the voice chat: the relay, the connection and who is heard.
    uint32_t (*GetVoiceStatus)(char* apBuffer, uint32_t aSize);

    // This game's voice key as 32 hex digits, to relay to the other players; 0 if apBuffer is too small.
    uint32_t (*GetVoiceKey)(char* apBuffer, uint32_t aSize);
    // A voice key relayed by another player (32 hex digits). Keys are forgotten when the game leaves the server.
    void (*AddVoiceKey)(uint32_t aPlayerId, const char* acpKey);
};

using TGetDescriptor = const Descriptor* (*)();
using THostApiVersion = uint32_t (*)();
} // namespace STRLipSync
