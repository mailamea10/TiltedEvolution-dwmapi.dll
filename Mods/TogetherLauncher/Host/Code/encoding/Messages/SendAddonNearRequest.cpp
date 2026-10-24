#include <Messages/SendAddonNearRequest.h>
#include <Messages/ModMessage.h>

void SendAddonNearRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ModMessage::WriteBytes(aWriter, Channel);
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteBool(aWriter, Unreliable);
    ModMessage::WriteBytes(aWriter, Data);
}

void SendAddonNearRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    // A malformed message gets an empty channel, which every handler rejects
    if (!ModMessage::ReadBytes(aReader, Channel))
        Channel.clear();
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Unreliable = Serialization::ReadBool(aReader);
    if (!ModMessage::ReadBytes(aReader, Data))
        Channel.clear();
}
