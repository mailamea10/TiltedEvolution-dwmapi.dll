#include <Services/ModMessageService.h>
#include <GameServer.h>

#include <Messages/ModMessage.h>
#include <Messages/NotifyModMessage.h>
#include <Messages/SendModMessageRequest.h>

#include <Events/PlayerLeaveEvent.h>
#include <Game/Player.h>
#include <World.h>

namespace
{
constexpr double kMessagesPerSecond = 20.0;
constexpr double kBurstSize = 40.0;
} // namespace

ModMessageService::ModMessageService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_sendModMessageConnection = aDispatcher.sink<PacketEvent<SendModMessageRequest>>().connect<&ModMessageService::OnSendModMessage>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&ModMessageService::OnPlayerLeave>(this);
}

void ModMessageService::OnSendModMessage(const PacketEvent<SendModMessageRequest>& acMessage) noexcept
{
    const auto& cRequest = acMessage.Packet;

    // Clients are not trusted: a modded one can send anything, so drop whatever the Plugin API would not have sent.
    // The rate limit comes first so that malformed messages count against it too.
    if (!TryConsume(*acMessage.pPlayer))
        return;

    if (!ModMessage::IsValidChannelName(cRequest.Channel) || cRequest.Data.size() > SkyrimTogetherAPI::kMaxMessageSize)
        return;

    const auto& cParty = acMessage.pPlayer->GetParty();
    if (!cParty.JoinedPartyId.has_value())
        return;

    NotifyModMessage notify{};
    notify.Channel = cRequest.Channel;
    notify.SenderPlayerId = acMessage.pPlayer->GetId();
    notify.Data = cRequest.Data;

    if (cRequest.TargetPlayerId == 0)
    {
        GameServer::Get()->SendToParty(notify, cParty, acMessage.GetSender());
        return;
    }

    // The target must be in the sender's own party, whatever the client claims
    const Player* pTarget = m_world.GetPlayerManager().GetById(cRequest.TargetPlayerId);
    if (!pTarget || pTarget == acMessage.pPlayer || pTarget->GetParty().JoinedPartyId != cParty.JoinedPartyId)
        return;

    pTarget->Send(notify);
}

void ModMessageService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    m_buckets.erase(acEvent.pPlayer->GetId());
}

bool ModMessageService::TryConsume(const Player& acPlayer) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    auto [itor, cIsNew] = m_buckets.try_emplace(acPlayer.GetId(), Bucket{kBurstSize, cNow});
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
        spdlog::warn("[ModMessageService]: Player {} ('{}') is sending Mod Messages too fast, dropping the excess", acPlayer.GetId(), acPlayer.GetUsername());
    }
    return false;
}
