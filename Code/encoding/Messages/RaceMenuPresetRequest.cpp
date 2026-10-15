#include <Messages/RaceMenuPresetRequest.h>

void RaceMenuPresetRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Revision);
    Serialization::WriteVarInt(aWriter, UncompressedSize);
    Serialization::WriteString(aWriter, Preset);
    FaceTints.Serialize(aWriter);
}

void RaceMenuPresetRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Revision = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    UncompressedSize = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Preset = Serialization::ReadString(aReader);
    FaceTints.Deserialize(aReader);
}
