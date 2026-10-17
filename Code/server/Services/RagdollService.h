#pragma once

#include <Events/PacketEvent.h>

struct World;
struct RagdollPoseRequest;

/**
 * @brief Relays ragdoll poses from the owner of each actor to the players in range of it (see RagdollPose).
 * Only the actor's owner may send its pose, so every client shows the one simulation that owner runs.
 */
class RagdollService
{
public:
    RagdollService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~RagdollService() noexcept = default;

    TP_NOCOPYMOVE(RagdollService);

private:
    void OnPoseRequest(const PacketEvent<RagdollPoseRequest>& acMessage) const noexcept;

    World& m_world;

    entt::scoped_connection m_poseConnection;
};
