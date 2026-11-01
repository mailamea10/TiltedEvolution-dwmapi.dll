#pragma once

// STR Mod Sync API, version 1.
//
// Lets an SKSE plugin have Skyrim Together share the local player's animation graph events (and the graph
// variables those events read) with the other players, without linking against Skyrim Together.
//
// The plugin exports one C function, STRModSync_GetDescriptor, returning a descriptor that lives as long as
// the plugin is loaded. The Skyrim Together add-on finds it by scanning the loaded modules, so load order
// does not matter. The add-on in turn exports STRModSync_HostApiVersion from the Skyrim Together launcher,
// which a plugin may look up (on the process's main module) to report whether the add-on is installed.
//
// Only events starting with a registered prefix are sent, and on the receiving side only events and
// variables that the receiver's own copy of the plugin registered are played, so every player needs the
// plugin and the add-on.

#include <cstdint>

namespace STRModSync
{
inline constexpr uint32_t kApiVersion = 1;

inline constexpr char kDescriptorExport[] = "STRModSync_GetDescriptor";
inline constexpr char kHostApiVersionExport[] = "STRModSync_HostApiVersion";

// Limits the add-on enforces.
inline constexpr uint32_t kMaxSources = 16;
inline constexpr uint32_t kMaxVariablesPerSource = 32;

enum VariableType : char
{
    kInt = 'i',
    kBool = 'b',
    kFloat = 'f',
};

struct Variable
{
    const char* Name;
    char Type; // VariableType
};

// Player graph events whose name starts with EventPrefix (case-insensitive) are shared. The variables are read
// from the player's graph when the event fires and set on the other players' copies of that player just
// before the event is played there.
struct GraphEventSource
{
    const char* EventPrefix;
    const Variable* Variables;
    uint32_t VariableCount;
};

struct Descriptor
{
    uint32_t ApiVersion; // kApiVersion
    const char* ModName;
    const char* ModVersion;
    // File name of the mod whose events are shared (for the log), or nullptr.
    const char* RequiredModule;
    const GraphEventSource* Sources;
    uint32_t SourceCount;
};

using TGetDescriptor = const Descriptor* (*)();
using THostApiVersion = uint32_t (*)();
} // namespace STRModSync
