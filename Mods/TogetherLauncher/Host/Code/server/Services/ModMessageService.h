#pragma once

#include <Events/PacketEvent.h>

struct World;
struct Player;
struct SendModMessageRequest;
struct PlayerLeaveEvent;

/**
 * @brief Relays Mod Messages sent through the Plugin API. The payload is never interpreted, only length-checked.
 */
struct ModMessageService
{
    ModMessageService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ModMessageService() noexcept = default;

    TP_NOCOPYMOVE(ModMessageService);

protected:
    void OnSendModMessage(const PacketEvent<SendModMessageRequest>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

private:
    // A token bucket per player: refills at a steady rate up to a burst size
    struct Bucket
    {
        double Tokens;
        std::chrono::steady_clock::time_point LastRefill;
        bool Dropping = false;
    };

    // False when the player is over the rate limit, in which case the message must be dropped
    [[nodiscard]] bool TryConsume(const Player& acPlayer) noexcept;

    World& m_world;
    Map<uint32_t, Bucket> m_buckets;

    entt::scoped_connection m_sendModMessageConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
