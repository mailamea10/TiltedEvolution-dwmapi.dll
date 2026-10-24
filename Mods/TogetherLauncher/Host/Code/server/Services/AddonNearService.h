#pragma once

#include <Events/PacketEvent.h>

struct World;
struct Player;
struct AddonHelloRequest;
struct SendAddonNearRequest;
struct PlayerLeaveEvent;

/**
 * @brief Relays Plugin API V2 messages about one actor to the players who have it in range. The payload is never
 * interpreted or stored, only length-checked, so the cost grows with the actors being synced and the players near
 * them, never with how long the server runs.
 */
struct AddonNearService
{
    AddonNearService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~AddonNearService() noexcept = default;

    TP_NOCOPYMOVE(AddonNearService);

protected:
    void OnHello(const PacketEvent<AddonHelloRequest>& acMessage) const noexcept;
    void OnSendNear(const PacketEvent<SendAddonNearRequest>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

private:
    // A token bucket per player and channel: refills at a steady rate up to a burst size
    struct Bucket
    {
        double Tokens;
        std::chrono::steady_clock::time_point LastRefill;
        bool Dropping = false;
    };

    // False when the player is over the rate limit on that channel, in which case the message must be dropped
    [[nodiscard]] bool TryConsume(const Player& acPlayer, const TiltedPhoques::String& acChannel) noexcept;

    World& m_world;
    // Keyed by player ID, then channel name
    Map<uint32_t, Map<TiltedPhoques::String, Bucket>> m_buckets;

    entt::scoped_connection m_helloConnection;
    entt::scoped_connection m_sendNearConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
