#pragma once

#include <mutex>

struct World;
struct TransportService;
struct UpdateEvent;
struct NiPoint3;
struct Actor;

/**
 * @brief Syncs animation scenes run by SexLab (and SexLab P+) to the other players.
 *
 * Classic SexLab starts each stage with Papyrus Debug.SendAnimationEvent, which STR already relays. SexLab P+
 * sends the stage events from its SKSE plugin (SexLabUtil.dll) straight to each actor's behavior graph, which
 * STR never saw, and it places every actor with SetPosition, which STR refuses for actors another client
 * owns - so neither the animations nor the placement of another player reached anyone.
 *
 * Here the graph notify of every character is watched, and events the scene plugin sends are relayed through
 * the existing script animation relay to every other client: the owner of that actor plays it on its own
 * actor (another player taking part then animates on their own game, and from there everyone else's), the
 * rest on their copy. Placement that STR refuses for another owner's actor is forwarded to that owner the
 * same way. The server needs no change.
 */
struct SceneSyncService
{
    SceneSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~SceneSyncService() noexcept = default;

    TP_NOCOPYMOVE(SceneSyncService);

    // Prefix of a forwarded placement in a script animation event name: "STRPOS:<x>:<y>:<z>:<rotation z>".
    static constexpr const char* kPlacementPrefix = "STRPOS:";

    // Whether the code at this return address belongs to a scene framework plugin.
    static bool IsSceneCaller(const void* apReturnAddress) noexcept;

    // From the graph notify hooks (any thread) and the SetPosition hook (game thread).
    static void CaptureEvent(uint32_t aFormId, const char* acpEvent) noexcept;
    static void CapturePlacement(uint32_t aFormId, const NiPoint3& acPosition, float aRotationZ) noexcept;

    // A relayed event the receiving actor's graph refused: try again for a moment, as the plugin does.
    void QueueRetry(uint32_t aFormId, const char* acpEvent) noexcept;

    // A placement forwarded to us for an actor we own.
    static void ApplyPlacement(Actor* apActor, const char* acpSpec) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void InstallHooks() noexcept;
    void SendCaptured() noexcept;
    void RunRetries(double aDelta) noexcept;

    struct Retry
    {
        uint32_t FormId;
        String Event;
        double Remaining;
        double Wait;
    };

    World& m_world;
    TransportService& m_transport;
    bool m_playerHooked{false};
    bool m_characterHooked{false};
    Vector<Retry> m_retries;

    entt::scoped_connection m_updateConnection;
};
