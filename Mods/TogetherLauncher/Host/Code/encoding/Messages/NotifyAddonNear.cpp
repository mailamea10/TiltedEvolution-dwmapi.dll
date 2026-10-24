#include <Messages/NotifyAddonNear.h>
#include <Messages/ModMessage.h>

void NotifyAddonNear::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ModMessage::WriteBytes(aWriter, Channel);
    Serialization::WriteVarInt(aWriter, SenderPlayerId);
    Serialization::WriteVarInt(aWriter, ServerId);
    ModMessage::WriteBytes(aWriter, Data);
}

void NotifyAddonNear::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    // A malformed message gets an empty channel, which no plugin has registered
    if (!ModMessage::ReadBytes(aReader, Channel))
        Channel.clear();
    SenderPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!ModMessage::ReadBytes(aReader, Data))
        Channel.clear();
}
