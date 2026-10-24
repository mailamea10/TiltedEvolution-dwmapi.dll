#include <Messages/AddonHelloRequest.h>

void AddonHelloRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ApiVersion);
}

void AddonHelloRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ApiVersion = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
