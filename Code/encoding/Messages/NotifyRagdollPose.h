#pragma once

#include "Message.h"
#include <Structs/RagdollPose.h>

/**
 * @brief Ragdoll poses relayed from the owner of each actor (see RagdollPoseRequest).
 */
struct NotifyRagdollPose final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyRagdollPose;

    NotifyRagdollPose()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRagdollPose& acRhs) const noexcept { return Tick == acRhs.Tick && Poses == acRhs.Poses && GetOpcode() == acRhs.GetOpcode(); }

    // The owner's clock tick at capture, the same timeline as its movement snapshots
    uint64_t Tick{};
    TiltedPhoques::Vector<RagdollPose> Poses{};
};
