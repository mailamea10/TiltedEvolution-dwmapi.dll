#include <Services/AddonNearService.h>
#include <Components.h>
#include <GameServer.h>

#include <Messages/AddonHelloRequest.h>
#include <Messages/ModMessage.h>
#include <Messages/NotifyAddonCapabilities.h>
#include <Messages/NotifyAddonNear.h>
#include <Messages/SendAddonNearRequest.h>

#include <Events/PlayerLeaveEvent.h>
#include <Game/OwnerView.h>
#include <Game/Player.h>
#include <World.h>

namespace
{
// Enough for a stream of 15 messages a second on several actors at once, per channel
constexpr double kMessagesPerSecond = 60.0;
constexpr double kBurstSize = 120.0;
constexpr size_t kMaxChannelsPerPlayer = 32;
} // namespace

AddonNearService::AddonNearService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_helloConnection = aDispatcher.sink<PacketEvent<AddonHelloRequest>>().connect<&AddonNearService::OnHello>(this);
    m_sendNearConnection = aDispatcher.sink<PacketEvent<SendAddonNearRequest>>().connect<&AddonNearService::OnSendNear>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&AddonNearService::OnPlayerLeave>(this);
}

void AddonNearService::OnHello(const PacketEvent<AddonHelloRequest>& acMessage) const noexcept
{
    NotifyAddonCapabilities notify{};
    notify.ApiVersion = static_cast<uint32_t>(SkyrimTogetherAPI::InterfaceVersion::V2);
    notify.MaxNearMessageSize = SkyrimTogetherAPI::kMaxNearMessageSize;
    acMessage.pPlayer->Send(notify);
}

void AddonNearService::OnSendNear(const PacketEvent<SendAddonNearRequest>& acMessage) noexcept
{
    const auto& cRequest = acMessage.Packet;

    // Clients are not trusted: a modded one can send anything, so drop whatever the Plugin API would not have sent.
    // The rate limit comes first so that malformed messages count against it too.
    if (!ModMessage::IsValidChannelName(cRequest.Channel) || !TryConsume(*acMessage.pPlayer, cRequest.Channel))
        return;

    if (cRequest.Data.size() > SkyrimTogetherAPI::kMaxNearMessageSize)
        return;

    // Only about an actor the sender controls, which includes its own character
    OwnerView<CellIdComponent> view(m_world, acMessage.GetSender());
    const auto cEntity = static_cast<entt::entity>(cRequest.ServerId);
    const auto cItor = view.find(cEntity);
    if (cItor == std::end(view))
    {
        spdlog::debug("[AddonNearService]: Player {} sent a message about {:x}, which it does not control", acMessage.pPlayer->GetId(), cRequest.ServerId);
        return;
    }

    const auto& cCellIdComponent = view.get<CellIdComponent>(*cItor);
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(cEntity);
    const bool cIsDragon = pCharacterComponent && pCharacterComponent->IsDragon();

    NotifyAddonNear notify{};
    notify.Channel = cRequest.Channel;
    notify.SenderPlayerId = acMessage.pPlayer->GetId();
    notify.ServerId = cRequest.ServerId;
    notify.Data = cRequest.Data;

    // Sent right away when unreliable: such streams are latest-wins, and Nagle would hold them back
    const auto cFlags = cRequest.Unreliable ? TiltedPhoques::kUnreliableNoNagle : TiltedPhoques::kReliable;
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == acMessage.pPlayer || !cCellIdComponent.IsInRange(pPlayer->GetCellComponent(), cIsDragon))
            continue;

        pPlayer->Send(notify, cFlags);
    }
}

void AddonNearService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    m_buckets.erase(acEvent.pPlayer->GetId());
}

bool AddonNearService::TryConsume(const Player& acPlayer, const TiltedPhoques::String& acChannel) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    auto& channels = m_buckets[acPlayer.GetId()];
    // A client inventing channel names must not grow this without bound
    if (channels.size() >= kMaxChannelsPerPlayer && channels.find(acChannel) == channels.end())
        return false;

    auto [itor, cIsNew] = channels.try_emplace(acChannel, Bucket{kBurstSize, cNow});
    Bucket& bucket = itor.value();

    if (!cIsNew)
    {
        const double cElapsed = std::chrono::duration<double>(cNow - bucket.LastRefill).count();
        bucket.Tokens = std::min(kBurstSize, bucket.Tokens + cElapsed * kMessagesPerSecond);
        bucket.LastRefill = cNow;
    }

    if (bucket.Tokens >= 1.0)
    {
        bucket.Tokens -= 1.0;
        bucket.Dropping = false;
        return true;
    }

    // Log once per burst, not once per dropped message
    if (!bucket.Dropping)
    {
        bucket.Dropping = true;
        spdlog::warn("[AddonNearService]: Player {} ('{}') is sending on channel '{}' too fast, dropping the excess", acPlayer.GetId(), acPlayer.GetUsername(), acChannel);
    }
    return false;
}
