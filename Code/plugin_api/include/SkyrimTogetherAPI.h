#pragma once

// Skyrim Together Plugin API, version 1.
//
// Lets an SKSE plugin sync its own state through Skyrim Together: the plugin registers a Channel and sends small
// Mod Messages on it, Skyrim Together relays them to the sender's party without interpreting them, and the same
// plugin on the other clients receives them. This header is self-contained, copy it into your plugin.
//
// Usage:
//   auto pRequest = reinterpret_cast<SkyrimTogetherAPI::RequestPluginAPI_t>(
//       GetProcAddress(GetModuleHandleW(nullptr), "RequestPluginAPI"));
//   // nullptr: not running under Skyrim Together, stay inactive.
//   auto* pApi = static_cast<SkyrimTogetherAPI::IVSkyrimTogether1*>(pRequest(SkyrimTogetherAPI::InterfaceVersion::V1));

#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherAPI
{
enum class InterfaceVersion : uint8_t
{
    V1 = 1
};

enum class SendResult : uint8_t
{
    Ok,           // Queued for sending. Delivery is not guaranteed.
    NotConnected, // Not connected to a server
    NotInParty,   // Not in a party, or the target is not in the local party
    TooLarge,     // Payload is longer than kMaxMessageSize
    BadChannel    // Not a handle returned by RegisterChannel
};

// A Mod Message is opaque bytes counted by length. It need not be text or null-terminated.
constexpr size_t kMaxMessageSize = 256;
// Channel names are 1 to kMaxChannelNameSize bytes of [A-Za-z0-9._-].
constexpr size_t kMaxChannelNameSize = 64;

using ChannelHandle = uint32_t; // 0 is invalid

// Both callbacks run during Skyrim Together's per-frame update, where its own sync code runs. That is not
// necessarily the game's main thread, and the thread can differ between calls. To change game state from the
// main thread, hand the work over with SKSE's task interface (SKSE::GetTaskInterface()->AddTask).

// aSenderId is the player ID the server saw the message come from.
using MessageCallback = void (*)(uint32_t aSenderId, const char* apBytes, size_t aLength);
// A player joined (aJoined) or left the local player's party.
using PartyChangedCallback = void (*)(uint32_t aPlayerId, bool aJoined);

class IVSkyrimTogether1
{
public:
    virtual ~IVSkyrimTogether1() = default;

    // Returns 0 if the name is invalid or already registered. Channels live until the process exits.
    // Either callback may be null.
    virtual ChannelHandle RegisterChannel(const char* apName, MessageCallback aOnMessage, PartyChangedCallback aOnPartyChanged) noexcept = 0;

    // Sends to the local player's party, sender excluded. Callable from any thread.
    virtual SendResult Send(ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept = 0;

    // Sends to one member of the local player's party. Callable from any thread.
    virtual SendResult SendTo(ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept = 0;

    // 0 while not connected.
    virtual uint32_t GetLocalPlayerId() const noexcept = 0;
    virtual bool IsInParty() const noexcept = 0;
};

// Exported by SkyrimTogether.exe. Returns nullptr for a version it does not know.
using RequestPluginAPI_t = void* (*)(InterfaceVersion aVersion);
} // namespace SkyrimTogetherAPI
