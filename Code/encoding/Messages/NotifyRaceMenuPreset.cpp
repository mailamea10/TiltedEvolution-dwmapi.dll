#include <Messages/NotifyRaceMenuPreset.h>

void NotifyRaceMenuPreset::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteVarInt(aWriter, Revision);
    Serialization::WriteVarInt(aWriter, UncompressedSize);
    Serialization::WriteString(aWriter, Preset);
    FaceTints.Serialize(aWriter);
}

void NotifyRaceMenuPreset::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Revision = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    UncompressedSize = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Preset = Serialization::ReadString(aReader);
    FaceTints.Deserialize(aReader);
}
