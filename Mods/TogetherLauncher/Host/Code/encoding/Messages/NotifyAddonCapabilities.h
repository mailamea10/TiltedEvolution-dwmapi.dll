#pragma once

#include "Message.h"

/**
 * @brief The server's answer to AddonHelloRequest: the Plugin API features it relays.
 */
struct NotifyAddonCapabilities final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyAddonCapabilities;

    NotifyAddonCapabilities()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyAddonCapabilities() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyAddonCapabilities& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ApiVersion == acRhs.ApiVersion && MaxNearMessageSize == acRhs.MaxNearMessageSize;
    }

    // The newest Plugin API version the server relays: 2 means SendNear
    uint32_t ApiVersion{0};
    uint32_t MaxNearMessageSize{0};
};
