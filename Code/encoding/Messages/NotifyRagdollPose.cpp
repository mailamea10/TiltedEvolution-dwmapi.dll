#include <Messages/NotifyRagdollPose.h>
#include <TiltedCore/Serialization.hpp>

#include <algorithm>

void NotifyRagdollPose::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Poses.size());
    for (const auto& pose : Poses)
        pose.Serialize(aWriter);
}

void NotifyRagdollPose::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto cCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), 64);
    Poses.resize(static_cast<size_t>(cCount));
    for (auto& pose : Poses)
        pose.Deserialize(aReader);
}
