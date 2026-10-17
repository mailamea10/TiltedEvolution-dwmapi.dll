#pragma once

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Stl.hpp>

// Serialization::WriteString/ReadString store a 16-bit length, so anything longer than 64 KiB is cut off.
// These write the full length for byte blobs that can be larger (a compressed RaceMenu preset is a few hundred
// KiB). Reading refuses lengths above acMaxSize and anything past the end of the packet.
namespace LargeBlob
{
inline void Write(TiltedPhoques::Buffer::Writer& aWriter, const TiltedPhoques::String& acData) noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, acData.size());
    aWriter.WriteBytes(reinterpret_cast<const uint8_t*>(acData.data()), acData.size());
}

inline TiltedPhoques::String Read(TiltedPhoques::Buffer::Reader& aReader, size_t aMaxSize) noexcept
{
    const uint64_t cSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
    if (cSize > aMaxSize)
        return {};

    TiltedPhoques::String data;
    data.resize(static_cast<size_t>(cSize));
    if (cSize && !aReader.ReadBytes(reinterpret_cast<uint8_t*>(data.data()), data.size()))
        return {};
    return data;
}
} // namespace LargeBlob
