#pragma once

#include <Events/PacketEvent.h>
#include <Structs/Tints.h>

struct World;
struct RaceMenuPresetRequest;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;

// Relays each player's RaceMenu appearance (sculpt, RaceMenu sliders and the rest of a RaceMenu preset) to
// the other players, and keeps the latest one so players who join later receive it as well.
struct RaceMenuService
{
    RaceMenuService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~RaceMenuService() noexcept = default;

    TP_NOCOPYMOVE(RaceMenuService);

protected:
    void OnPresetRequest(const PacketEvent<RaceMenuPresetRequest>& acMessage) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) const noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

private:
    struct StoredPreset
    {
        uint32_t Revision{};
        uint32_t UncompressedSize{};
        String Preset{};
        Tints FaceTints{};
    };

    World& m_world;
    Map<uint32_t, StoredPreset> m_presets;

    entt::scoped_connection m_presetConnection;
    entt::scoped_connection m_joinConnection;
    entt::scoped_connection m_leaveConnection;
};
