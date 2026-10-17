#include <Services/RagdollService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Game/OwnerView.h>

#include <Messages/NotifyRagdollPose.h>
#include <Messages/RagdollPoseRequest.h>

RagdollService::RagdollService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_poseConnection = aDispatcher.sink<PacketEvent<RagdollPoseRequest>>().connect<&RagdollService::OnPoseRequest>(this);
}

void RagdollService::OnPoseRequest(const PacketEvent<RagdollPoseRequest>& acMessage) const noexcept
{
    OwnerView<CellIdComponent, CharacterComponent> view(m_world, acMessage.GetSender());

    const auto& packet = acMessage.Packet;

    // Streaming poses can be lost (the next one replaces them); the ones that end a ragdoll or hold a body at
    // rest must arrive.
    Map<Player*, NotifyRagdollPose> streaming;
    Map<Player*, NotifyRagdollPose> held;

    for (const auto& pose : packet.Poses)
    {
        const auto cEntity = static_cast<entt::entity>(pose.ServerId);
        const auto itor = view.find(cEntity);
        if (itor == std::end(view))
        {
            spdlog::debug("{:x} sent a ragdoll pose for {:x}, which it does not own", acMessage.pPlayer->GetConnectionId(), pose.ServerId);
            continue;
        }

        const auto& cellIdComponent = view.get<CellIdComponent>(*itor);
        const auto& characterComponent = view.get<CharacterComponent>(*itor);
        const bool cHeld = (pose.PoseFlags & (RagdollPose::kEnd | RagdollPose::kRest)) != 0;

        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == acMessage.pPlayer || !cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& relay = (cHeld ? held : streaming)[pPlayer];
            relay.Tick = packet.Tick;
            relay.Poses.push_back(pose);
        }
    }

    for (auto& [pPlayer, relay] : held)
        pPlayer->Send(relay);
    for (auto& [pPlayer, relay] : streaming)
        pPlayer->Send(relay, TiltedPhoques::kUnreliableNoNagle);
}
