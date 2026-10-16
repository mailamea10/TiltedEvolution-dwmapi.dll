#pragma once

#include <mutex>

struct World;
struct TransportService;
struct UpdateEvent;
struct BSFixedString;

/**
 * @brief Syncs animation graph events that SKSE mods send straight to the player's behavior graph.
 *
 * STR replays the player's actions (BGSAction) and Papyrus/console animation events, but a plugin that calls
 * NotifyAnimationGraph on the player from C++ - SkyParkour for every vault, climb, grab, step and slide -
 * bypasses both, so the other players only ever saw the position change. The player's graph notify is
 * hooked, events from the known mods are captured together with the graph variables they depend on, and
 * both travel through the existing script animation relay, so the server needs no change.
 */
struct GraphEventSyncService
{
    GraphEventSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~GraphEventSyncService() noexcept = default;

    TP_NOCOPYMOVE(GraphEventSyncService);

    // Prefix carried by graph variable updates in a script animation event name: "STRGV:<type>:<name>:<value>".
    static constexpr const char* kVariablePrefix = "STRGV:";

    // Called from the player's graph notify hook, on whatever thread the game notified from.
    static void Capture(const char* acpEvent) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void InstallHook() noexcept;

    World& m_world;
    TransportService& m_transport;
    bool m_hookInstalled{false};

    entt::scoped_connection m_updateConnection;
};
