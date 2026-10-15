#pragma once

#include "Message.h"
#include <Structs/Tints.h>

// The owner's RaceMenu appearance (a RaceMenu .jslot preset, zlib compressed) and its current face tints.
struct RaceMenuPresetRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRaceMenuPresetRequest;

    RaceMenuPresetRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RaceMenuPresetRequest& acRhs) const noexcept
    {
        return Revision == acRhs.Revision && UncompressedSize == acRhs.UncompressedSize && Preset == acRhs.Preset && FaceTints == acRhs.FaceTints &&
               GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t Revision{};
    uint32_t UncompressedSize{};
    String Preset{};
    Tints FaceTints{};
};
