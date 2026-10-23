#pragma once

#include "Message.h"

#include <SkyrimTogetherAPI.h>

// Helpers shared by the two Plugin API messages and the services that handle them.
namespace ModMessage
{
// 1 to kMaxChannelNameSize bytes of [A-Za-z0-9._-]
inline bool IsValidChannelName(const TiltedPhoques::String& acName) noexcept
{
    if (acName.empty() || acName.size() > SkyrimTogetherAPI::kMaxChannelNameSize)
        return false;

    for (const char c : acName)
    {
        const bool cIsValid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!cIsValid)
            return false;
    }
    return true;
}

inline void WriteBytes(TiltedPhoques::Buffer::Writer& aWriter, const TiltedPhoques::String& acBytes) noexcept
{
    Serialization::WriteVarInt(aWriter, acBytes.size());
    aWriter.WriteBytes(reinterpret_cast<const uint8_t*>(acBytes.data()), acBytes.size());
}

// Unlike Serialization::ReadString, a length prefix larger than what is left in the packet is rejected before
// anything is allocated, as these messages come from untrusted clients. Returns false on a malformed length.
inline bool ReadBytes(TiltedPhoques::Buffer::Reader& aReader, TiltedPhoques::String& aBytes) noexcept
{
    aBytes.clear();

    const uint64_t cLength = Serialization::ReadVarInt(aReader);
    if (cLength > aReader.GetBuffer()->GetSize() - aReader.GetBytePosition())
        return false;

    aBytes.resize(cLength);
    return aReader.ReadBytes(reinterpret_cast<uint8_t*>(aBytes.data()), cLength);
}
} // namespace ModMessage
