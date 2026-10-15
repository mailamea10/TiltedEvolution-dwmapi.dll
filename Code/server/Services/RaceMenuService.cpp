#include <Services/RaceMenuService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Messages/NotifyRaceMenuPreset.h>
#include <Messages/RaceMenuPresetRequest.h>

namespace
{
// A RaceMenu preset with sculpting compresses to a few dozen KiB; anything near the network message limit
// is not an appearance and is not relayed.
constexpr size_t kMaxPresetBytes = 384 * 1024;
} // namespace

RaceMenuService::RaceMenuService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_presetConnection = aDispatcher.sink<PacketEvent<RaceMenuPresetRequest>>().connect<&RaceMenuService::OnPresetRequest>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&RaceMenuService::OnPlayerJoin>(this);
    m_leaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&RaceMenuService::OnPlayerLeave>(this);
}

void RaceMenuService::OnPresetRequest(const PacketEvent<RaceMenuPresetRequest>& acMessage) noexcept
{
    const auto& packet = acMessage.Packet;
    Player* pSender = acMessage.pPlayer;

    if (packet.Preset.empty() || packet.Preset.size() > kMaxPresetBytes)
    {
        spdlog::warn("RaceMenu preset from player {:X} ignored: {} bytes", pSender->GetId(), packet.Preset.size());
        return;
    }

    auto& stored = m_presets[pSender->GetId()];
    stored.Revision = packet.Revision;
    stored.UncompressedSize = packet.UncompressedSize;
    stored.Preset = packet.Preset;
    stored.FaceTints = packet.FaceTints;

    NotifyRaceMenuPreset notify{};
    notify.PlayerId = pSender->GetId();
    notify.Revision = stored.Revision;
    notify.UncompressedSize = stored.UncompressedSize;
    notify.Preset = stored.Preset;
    notify.FaceTints = stored.FaceTints;

    spdlog::info("RaceMenu preset revision {} from player {:X} ({} bytes), relaying", packet.Revision, pSender->GetId(), packet.Preset.size());

    GameServer::Get()->SendToPlayers(notify, pSender);
}

void RaceMenuService::OnPlayerJoin(const PlayerJoinEvent& acEvent) const noexcept
{
    Player* pJoined = acEvent.pPlayer;

    for (const auto& [playerId, stored] : m_presets)
    {
        if (playerId == pJoined->GetId())
            continue;

        NotifyRaceMenuPreset notify{};
        notify.PlayerId = playerId;
        notify.Revision = stored.Revision;
        notify.UncompressedSize = stored.UncompressedSize;
        notify.Preset = stored.Preset;
        notify.FaceTints = stored.FaceTints;

        pJoined->Send(notify);
    }
}

void RaceMenuService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    m_presets.erase(acEvent.pPlayer->GetId());
}
