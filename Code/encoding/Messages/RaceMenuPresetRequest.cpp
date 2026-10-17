#include <Messages/RaceMenuPresetRequest.h>
#include <Structs/LargeBlob.h>

// Matches the server and client limits for a compressed preset.
constexpr size_t kMaxPresetBytes = 384 * 1024;

void RaceMenuPresetRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Revision);
    Serialization::WriteVarInt(aWriter, UncompressedSize);
    LargeBlob::Write(aWriter, Preset);
    FaceTints.Serialize(aWriter);
}

void RaceMenuPresetRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Revision = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    UncompressedSize = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Preset = LargeBlob::Read(aReader, kMaxPresetBytes);
    FaceTints.Deserialize(aReader);
}
