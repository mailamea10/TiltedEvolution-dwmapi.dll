#include <Structs/RagdollPose.h>
#include <TiltedCore/Serialization.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using TiltedPhoques::Serialization;

namespace
{
// Offsets from the first bone, in 1/64 game units: +-512 units covers any skeleton.
constexpr float kOffsetScale = 64.f;

void WriteFloat(TiltedPhoques::Buffer::Writer& aWriter, float aValue) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &aValue, sizeof(bits));
    aWriter.WriteBits(bits, 32);
}

float ReadFloat(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    const uint32_t bits = static_cast<uint32_t>(tmp);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void WriteInt16(TiltedPhoques::Buffer::Writer& aWriter, float aValue) noexcept
{
    const auto cClamped = static_cast<int32_t>(std::lround(std::clamp(aValue, -32767.f, 32767.f)));
    aWriter.WriteBits(static_cast<uint16_t>(static_cast<int16_t>(cClamped)), 16);
}

float ReadInt16(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 16);
    return static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(tmp)));
}
} // namespace

bool RagdollPose::Bone::operator==(const Bone& acRhs) const noexcept
{
    return NameHash == acRhs.NameHash && std::equal(std::begin(Position), std::end(Position), std::begin(acRhs.Position)) &&
           std::equal(std::begin(Rotation), std::end(Rotation), std::begin(acRhs.Rotation));
}

bool RagdollPose::operator==(const RagdollPose& acRhs) const noexcept
{
    return ServerId == acRhs.ServerId && PoseFlags == acRhs.PoseFlags && Bones == acRhs.Bones;
}

void RagdollPose::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    aWriter.WriteBits(PoseFlags, 8);

    const size_t cCount = std::min(Bones.size(), kMaxBones);
    aWriter.WriteBits(cCount, 8);

    for (size_t i = 0; i < cCount; ++i)
    {
        const auto& bone = Bones[i];
        aWriter.WriteBits(bone.NameHash, 32);

        if (i == 0)
        {
            for (const float cValue : bone.Position)
                WriteFloat(aWriter, cValue);
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
                WriteInt16(aWriter, (bone.Position[axis] - Bones[0].Position[axis]) * kOffsetScale);
        }

        // Normalised quaternion, 16 bits per component.
        for (const float cValue : bone.Rotation)
            WriteInt16(aWriter, cValue * 32767.f);
    }
}

void RagdollPose::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);
    PoseFlags = static_cast<uint8_t>(tmp);

    aReader.ReadBits(tmp, 8);
    const size_t cCount = std::min(static_cast<size_t>(tmp & 0xFF), kMaxBones);

    Bones.clear();
    Bones.reserve(cCount);
    for (size_t i = 0; i < cCount; ++i)
    {
        Bone bone;
        aReader.ReadBits(tmp, 32);
        bone.NameHash = static_cast<uint32_t>(tmp);

        if (i == 0)
        {
            for (float& value : bone.Position)
                value = ReadFloat(aReader);
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
                bone.Position[axis] = Bones[0].Position[axis] + ReadInt16(aReader) / kOffsetScale;
        }

        float length = 0.f;
        for (float& value : bone.Rotation)
        {
            value = ReadInt16(aReader) / 32767.f;
            length += value * value;
        }
        length = std::sqrt(length);
        if (length > 1e-4f)
        {
            for (float& value : bone.Rotation)
                value /= length;
        }
        else
        {
            bone.Rotation[0] = bone.Rotation[1] = bone.Rotation[2] = 0.f;
            bone.Rotation[3] = 1.f;
        }

        Bones.push_back(bone);
    }
}
