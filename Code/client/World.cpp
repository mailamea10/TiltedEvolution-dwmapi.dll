#include <TiltedOnlinePCH.h>

#include "World.h"

#include <Services/DiscoveryService.h>
#include <Services/InputService.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/NameTagService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/TradeService.h>
#include <Services/CalendarService.h>
#include <Services/StringCacheService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/MapService.h>
#include <Services/DroppedItemService.h>
#include <Services/PluginService.h>

#include <Messages/NotifyModMessage.h>
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPartyJoined.h>
#include <Messages/NotifyPartyLeft.h>

#include <Events/PreUpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>  

World::World()
    : m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{
    ctx().emplace<ImguiService>();
    ctx().emplace<DiscoveryService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher);
    ctx().emplace<InputService>(ctx().at<OverlayService>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>();
    ctx().emplace<DiscordService>(m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);
    ctx().emplace<TradeService>(*this, m_dispatcher, m_transport);
    ctx().emplace<NameTagService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport);
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
    ctx().emplace<StringCacheService>(m_dispatcher);
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_transport, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DroppedItemService>(*this, m_dispatcher, m_transport);

    // The Plugin API outlives the World, so there is nothing to disconnect
    m_dispatcher.sink<NotifyModMessage>().connect<&PluginService::OnNotifyModMessage>(PluginService::Get());
    // These come after PartyService's own handlers, so its member list is up to date by then
    m_dispatcher.sink<NotifyPartyInfo>().connect<&PluginService::OnPartyEvent<NotifyPartyInfo>>(PluginService::Get());
    m_dispatcher.sink<NotifyPartyJoined>().connect<&PluginService::OnPartyEvent<NotifyPartyJoined>>(PluginService::Get());
    m_dispatcher.sink<NotifyPartyLeft>().connect<&PluginService::OnPartyEvent<NotifyPartyLeft>>(PluginService::Get());
    m_dispatcher.sink<DisconnectedEvent>().connect<&PluginService::OnPartyEvent<DisconnectedEvent>>(PluginService::Get());

    BehaviorVar::Get()->Init();
}

World::~World() = default;

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;

    // A window timer now drives this in addition to the game's own vm tick (which
    // never fires on 1.5.97), so run at most one update per ~8ms and let the two
    // sources interleave rather than doubling the rate. A skipped call leaves
    // m_lastFrameTime untouched, so the next accepted one measures its delta from
    // the last real update.
    if (m_hasTicked && cDelta < std::chrono::milliseconds(8))
        return;

    m_hasTicked = true;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
}

uint64_t World::GetTick() const noexcept
{
    return m_transport.GetClock().GetCurrentTick();
}

void World::Create() noexcept
{
    if (!entt::locator<World>::has_value())
    {
        entt::locator<World>::emplace();
    }
}

World& World::Get() noexcept
{
    return entt::locator<World>::value();
}
