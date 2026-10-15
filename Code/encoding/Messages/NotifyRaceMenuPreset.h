#pragma once

#include "Message.h"
#include <Structs/Tints.h>

// Another player's RaceMenu appearance, relayed by the server (see RaceMenuPresetRequest).
struct NotifyRaceMenuPreset final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyRaceMenuPreset;

    NotifyRaceMenuPreset()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRaceMenuPreset& acRhs) const noexcept
    {
        return PlayerId == acRhs.PlayerId && Revision == acRhs.Revision && UncompressedSize == acRhs.UncompressedSize && Preset == acRhs.Preset &&
               FaceTints == acRhs.FaceTints && GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t PlayerId{};
    uint32_t Revision{};
    uint32_t UncompressedSize{};
    String Preset{};
    Tints FaceTints{};
};
