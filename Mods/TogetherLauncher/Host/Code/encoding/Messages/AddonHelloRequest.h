#pragma once

#include "Message.h"

/**
 * @brief Sent once after authentication: asks which Plugin API features the server relays. A server without them
 * cannot parse it and drops it, which the client takes as "no".
 */
struct AddonHelloRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAddonHelloRequest;

    AddonHelloRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AddonHelloRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AddonHelloRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ApiVersion == acRhs.ApiVersion; }

    // The newest Plugin API version the client implements
    uint32_t ApiVersion{0};
};
