#pragma once

// STR RaceMenu API, version 1.
//
// The contract between the RaceMenu Together SKSE plugin (RaceMenuTogether.dll) and the RaceMenu Together
// add-on built into the Skyrim Together launcher.
//
// The plugin exports one C function, STRRaceMenu_GetDescriptor, returning a descriptor that lives as long as
// the plugin is loaded. The add-on finds it by scanning the loaded modules, so load order does not matter,
// and stays off until it has found it. The add-on in turn exports STRRaceMenu_HostApiVersion from the
// launcher, which the plugin looks up (in every loaded module) to report whether the add-on is installed.

#include <cstdint>

namespace STRRaceMenu
{
inline constexpr uint32_t kApiVersion = 1;

inline constexpr char kDescriptorExport[] = "STRRaceMenu_GetDescriptor";
inline constexpr char kHostApiVersionExport[] = "STRRaceMenu_HostApiVersion";

inline constexpr uint32_t kMaxProtectedMorphKeys = 32;

struct Descriptor
{
    uint32_t ApiVersion; // kApiVersion
    const char* ModName;
    const char* ModVersion;

    // Body morph keys that other mods put on NPCs (BodyGen, OBody, ...). A copy of another player counts as an
    // NPC to them, so morphs under these keys on a copy are cleared and the owner's morphs put back, unless the
    // owner's own appearance uses that key.
    const char* const* ProtectedMorphKeys;
    uint32_t ProtectedMorphKeyCount;

    // The version of RaceMenu's body morph interface the plugin got from RaceMenu, or 0 when RaceMenu did not
    // answer (not installed, or too old to hand out its interfaces). Only used for the log.
    uint32_t (*GetRaceMenuInterfaceVersion)();
};

using TGetDescriptor = const Descriptor* (*)();
using THostApiVersion = uint32_t (*)();
} // namespace STRRaceMenu
