#include <Messages/NotifyModMessage.h>
#include <Messages/ModMessage.h>

void NotifyModMessage::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ModMessage::WriteBytes(aWriter, Channel);
    Serialization::WriteVarInt(aWriter, SenderPlayerId);
    ModMessage::WriteBytes(aWriter, Data);
}

void NotifyModMessage::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    // A malformed message gets an empty channel, which no plugin has registered
    if (!ModMessage::ReadBytes(aReader, Channel))
        Channel.clear();
    SenderPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!ModMessage::ReadBytes(aReader, Data))
        Channel.clear();
}
