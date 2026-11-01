#include <Messages/NotifyAddonCapabilities.h>

void NotifyAddonCapabilities::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ApiVersion);
    Serialization::WriteVarInt(aWriter, MaxNearMessageSize);
}

void NotifyAddonCapabilities::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ApiVersion = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    MaxNearMessageSize = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
