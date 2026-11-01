#pragma once

#include "Message.h"

/**
 * @brief A Plugin API V2 message about one actor, relayed to a player who has it in range. The sender is stamped by
 * the server, never taken from the payload.
 */
struct NotifyAddonNear final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyAddonNear;

    NotifyAddonNear()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyAddonNear() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyAddonNear& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Channel == acRhs.Channel && SenderPlayerId == acRhs.SenderPlayerId && ServerId == acRhs.ServerId && Data == acRhs.Data;
    }

    TiltedPhoques::String Channel;
    uint32_t SenderPlayerId{0};
    uint32_t ServerId{0};
    TiltedPhoques::String Data;
};
