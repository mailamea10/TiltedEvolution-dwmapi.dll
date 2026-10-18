#pragma once

// STR LipSync API, version 2.
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

#include <cstdint>

namespace STRLipSync
{
inline constexpr uint32_t kApiVersion = 2;

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
};

using TGetDescriptor = const Descriptor* (*)();
using THostApiVersion = uint32_t (*)();
} // namespace STRLipSync
