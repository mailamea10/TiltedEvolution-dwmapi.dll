#pragma once

#include "Message.h"
#include <Structs/RagdollPose.h>

/**
 * @brief Ragdoll poses of actors the sender owns (see RagdollPose). Streaming poses go unreliable; a pose that
 * ends a ragdoll or holds a body at rest goes reliable.
 */
struct RagdollPoseRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRagdollPoseRequest;

    RagdollPoseRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RagdollPoseRequest& acRhs) const noexcept { return Tick == acRhs.Tick && Poses == acRhs.Poses && GetOpcode() == acRhs.GetOpcode(); }

    // The owner's clock tick at capture, the same timeline as its movement snapshots
    uint64_t Tick{};
    TiltedPhoques::Vector<RagdollPose> Poses{};
};
