#pragma once

#include "Message.h"

/**
 * @brief A Plugin API V2 message about one actor, for every other player who has that actor in range. Opaque to
 * Skyrim Together, and never cached: the payload is plain bytes, not a CachedString.
 */
struct SendAddonNearRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kSendAddonNearRequest;

    SendAddonNearRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~SendAddonNearRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SendAddonNearRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Channel == acRhs.Channel && ServerId == acRhs.ServerId && Unreliable == acRhs.Unreliable && Data == acRhs.Data;
    }

    TiltedPhoques::String Channel;
    uint32_t ServerId{0};
    // Relayed the way it came in: a lost unreliable message is replaced by the next one
    bool Unreliable{false};
    TiltedPhoques::String Data;
};
