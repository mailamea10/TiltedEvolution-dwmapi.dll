#pragma once

#include "Message.h"

/**
 * @brief A Mod Message from a plugin, for the sender's party or one party member. Opaque to Skyrim Together.
 */
struct SendModMessageRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kSendModMessageRequest;

    SendModMessageRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~SendModMessageRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SendModMessageRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Channel == acRhs.Channel && TargetPlayerId == acRhs.TargetPlayerId && Data == acRhs.Data;
    }

    TiltedPhoques::String Channel;
    // 0 sends to the whole party
    uint32_t TargetPlayerId{0};
    TiltedPhoques::String Data;
};
