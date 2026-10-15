#pragma once

#include <Structs/Tints.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifyRaceMenuPreset;
struct Actor;

/**
 * @brief Syncs what RaceMenu adds on top of the vanilla face: sculpting (including heads imported on the
 * sculpt tab), RaceMenu's own sliders, body morphs, node transforms and the rest of a RaceMenu preset
 * (including one loaded from the preset tab).
 *
 * The owner's appearance is taken with RaceMenu's own preset export when RaceMenu closes (and once after
 * joining), sent through the server, and put on the other players' copies with RaceMenu's own preset
 * loader. RaceMenu therefore stays the authority on its format: nothing here parses a preset.
 */
struct RaceMenuSyncService
{
    RaceMenuSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~RaceMenuSyncService() noexcept = default;

    TP_NOCOPYMOVE(RaceMenuSyncService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnNotifyPreset(const NotifyRaceMenuPreset& acMessage) noexcept;

private:
    struct RemotePreset
    {
        uint32_t Revision{};
        String Json{};
        Tints FaceTints{};

        // What the preset was last put on, so a respawned actor or a reloaded body gets it again.
        uint32_t AppliedFormId{};
        uint32_t AppliedRevision{};
        const void* AppliedRoot{};
        uint32_t FailedAttempts{};
    };

    bool RaceMenuAvailable() noexcept;
    void SendLocalPreset(const char* acpReason) noexcept;
    void ApplyPending() noexcept;
    bool ApplyPreset(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept;

    World& m_world;
    TransportService& m_transport;

    Map<uint32_t, RemotePreset> m_remotePresets;

    bool m_raceMenuWasOpen{false};
    bool m_sendAfterJoin{false};
    double m_joinDelay{0.0};
    double m_applyTimer{0.0};
    uint32_t m_localRevision{0};
    bool m_reportedMissing{false};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_notifyConnection;
};
