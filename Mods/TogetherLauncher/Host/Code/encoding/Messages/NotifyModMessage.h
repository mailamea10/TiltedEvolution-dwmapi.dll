#pragma once

#include "Message.h"

/**
 * @brief A Mod Message relayed to a plugin. The sender is stamped by the server, never taken from the payload.
 */
struct NotifyModMessage final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyModMessage;

    NotifyModMessage()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyModMessage() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyModMessage& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Channel == acRhs.Channel && SenderPlayerId == acRhs.SenderPlayerId && Data == acRhs.Data;
    }

    TiltedPhoques::String Channel;
    uint32_t SenderPlayerId{0};
    TiltedPhoques::String Data;
};
