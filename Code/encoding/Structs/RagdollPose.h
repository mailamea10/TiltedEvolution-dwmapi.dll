#pragma once

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

/**
 * @brief Where an actor's ragdoll bodies are on its owner's screen, in world space.
 *
 * One entry per skeleton bone that carries a ragdoll body, matched across clients by a hash of the bone's
 * name. The first bone is sent at full precision, the others relative to it.
 */
struct RagdollPose
{
    enum Flags : uint8_t
    {
        // The ragdoll ended (the actor is getting up); blend back to the animation.
        kEnd = 1 << 0,
        // The bodies came to rest; this pose holds until the next one.
        kRest = 1 << 1,
    };

    struct Bone
    {
        uint32_t NameHash{};
        float Position[3]{};
        // x, y, z, w
        float Rotation[4]{0.f, 0.f, 0.f, 1.f};

        bool operator==(const Bone& acRhs) const noexcept;
    };

    // The relayed size is bounded; a skeleton has fewer ragdoll bodies than this.
    static constexpr size_t kMaxBones = 48;

    bool operator==(const RagdollPose& acRhs) const noexcept;
    bool operator!=(const RagdollPose& acRhs) const noexcept { return !(*this == acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t ServerId{};
    uint8_t PoseFlags{};
    TiltedPhoques::Vector<Bone> Bones{};
};
