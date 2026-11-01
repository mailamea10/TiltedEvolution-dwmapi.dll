#pragma once

// STR Ragdoll API, version 1.
//
// The contract between the Ragdoll Together SKSE plugin (RagdollTogether.dll) and the Ragdoll Together add-on built
// into the Skyrim Together launcher.
//
// The plugin exports one C function, STRRagdoll_GetDescriptor, returning a descriptor that lives as long as the
// plugin is loaded. The add-on finds it by scanning the loaded modules, so load order does not matter, and stays off
// until it has found it. The add-on in turn exports STRRagdoll_HostApiVersion from the launcher, which the plugin
// looks up (in every loaded module) to report whether the add-on is installed.

#include <cstdint>

namespace STRRagdoll
{
inline constexpr uint32_t kApiVersion = 1;

inline constexpr char kDescriptorExport[] = "STRRagdoll_GetDescriptor";
inline constexpr char kHostApiVersionExport[] = "STRRagdoll_HostApiVersion";

struct Descriptor
{
    uint32_t ApiVersion; // kApiVersion
    const char* ModName;
    const char* ModVersion;
};

using TGetDescriptor = const Descriptor* (*)();
using THostApiVersion = uint32_t (*)();
} // namespace STRRagdoll
