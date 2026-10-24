#include <Services/PluginService.h>

#include <World.h>
#include <Components.h>

#include <Messages/ModMessage.h>
#include <Messages/NotifyModMessage.h>
#include <Messages/SendModMessageRequest.h>
#include <Messages/AddonHelloRequest.h>
#include <Messages/NotifyAddonCapabilities.h>
#include <Messages/NotifyAddonNear.h>
#include <Messages/SendAddonNearRequest.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

using namespace SkyrimTogetherAPI;

PluginService& PluginService::Get() noexcept
{
    static PluginService s_instance;
    return s_instance;
}

ChannelHandle PluginService::RegisterChannel(const char* apName, MessageCallback aOnMessage, PartyChangedCallback aOnPartyChanged) noexcept
{
    // Reading up to one byte past the limit is enough to tell the name is too long
    const String cName = apName ? String(apName, strnlen(apName, kMaxChannelNameSize + 1)) : String();
    if (!ModMessage::IsValidChannelName(cName))
        return 0;

    std::lock_guard cLock(m_lock);

    for (const Channel& cChannel : m_channels)
    {
        if (cChannel.Name == cName)
            return 0;
    }

    m_channels.push_back({cName, aOnMessage, aOnPartyChanged});
    return static_cast<ChannelHandle>(m_channels.size());
}

SendResult PluginService::Send(ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept
{
    return Queue(aChannel, 0, apBytes, aLength);
}

SendResult PluginService::SendTo(ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept
{
    // 0 means the whole party, so it is not a valid target
    if (aPlayerId == 0)
        return SendResult::NotInParty;

    return Queue(aChannel, aPlayerId, apBytes, aLength);
}

uint32_t PluginService::GetLocalPlayerId() const noexcept
{
    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return 0;

    return World::Get().GetTransport().GetLocalPlayerId();
}

bool PluginService::IsInParty() const noexcept
{
    return entt::locator<World>::has_value() && World::Get().GetPartyService().IsInParty();
}

SendResult PluginService::Queue(ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept
{
    String channelName;
    {
        std::lock_guard cLock(m_lock);

        if (aChannel == 0 || aChannel > m_channels.size())
            return SendResult::BadChannel;

        channelName = m_channels[aChannel - 1].Name;
    }

    if (aLength > kMaxMessageSize)
        return SendResult::TooLarge;

    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return SendResult::NotConnected;

    const bool cToAll = aTargetPlayerId == ModMessage::kAllPlayers;
    if (cToAll && !m_nearSupported)
        return SendResult::NotSupported;

    if (!cToAll && !World::Get().GetPartyService().IsInParty())
        return SendResult::NotInParty;

    if (aTargetPlayerId != 0 && !cToAll)
    {
        std::lock_guard cLock(m_lock);

        if (!std::binary_search(m_members.begin(), m_members.end(), aTargetPlayerId))
            return SendResult::NotInParty;
    }

    // The state may have changed by the time the World update gets to it, in which case the message is dropped
    World::Get().GetRunner().Queue(
        [channelName = std::move(channelName), aTargetPlayerId, cToAll, data = String(apBytes ? apBytes : "", apBytes ? aLength : 0)]()
        {
            auto& world = World::Get();
            if (!world.GetTransport().IsOnline() || (!cToAll && !world.GetPartyService().IsInParty()))
                return;

            SendModMessageRequest request{};
            request.Channel = channelName;
            request.TargetPlayerId = aTargetPlayerId;
            request.Data = data;
            world.GetTransport().Send(request);
        });

    return SendResult::Ok;
}

void PluginService::OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept
{
    MessageCallback onMessage = nullptr;
    {
        std::lock_guard cLock(m_lock);

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.Name == acMessage.Channel)
            {
                onMessage = cChannel.OnMessage;
                break;
            }
        }
    }

    // No plugin registered that channel on this client: nothing to do
    if (onMessage)
        onMessage(acMessage.SenderPlayerId, acMessage.Data.data(), acMessage.Data.size());
}

void PluginService::NotifyPartyChanges() noexcept
{
    const uint32_t cLocalId = GetLocalPlayerId();
    const auto& cParty = World::Get().GetPartyService();

    Vector<uint32_t> members;
    if (cParty.IsInParty())
    {
        for (const uint32_t cId : cParty.GetPartyMembers())
        {
            if (cId != cLocalId)
                members.push_back(cId);
        }
    }
    std::sort(members.begin(), members.end());

    Vector<uint32_t> joined, left;
    Vector<PartyChangedCallback> callbacks;
    {
        std::lock_guard cLock(m_lock);

        std::set_difference(members.begin(), members.end(), m_members.begin(), m_members.end(), std::back_inserter(joined));
        std::set_difference(m_members.begin(), m_members.end(), members.begin(), members.end(), std::back_inserter(left));
        m_members = members;

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.OnPartyChanged)
                callbacks.push_back(cChannel.OnPartyChanged);
        }
    }

    for (const PartyChangedCallback cCallback : callbacks)
    {
        for (const uint32_t cId : joined)
            cCallback(cId, true);
        for (const uint32_t cId : left)
            cCallback(cId, false);
    }
}

bool PluginService::SetNearMessageCallback(ChannelHandle aChannel, NearMessageCallback aOnNearMessage) noexcept
{
    std::lock_guard cLock(m_lock);

    if (aChannel == 0 || aChannel > m_channels.size())
        return false;

    m_channels[aChannel - 1].OnNearMessage = aOnNearMessage;
    if (aOnNearMessage)
        m_anyNearChannel = true;
    return true;
}

SendResult PluginService::SendNear(ChannelHandle aChannel, uint32_t aServerId, Delivery aDelivery, const char* apBytes, size_t aLength) noexcept
{
    String channelName;
    {
        std::lock_guard cLock(m_lock);

        if (aChannel == 0 || aChannel > m_channels.size())
            return SendResult::BadChannel;

        channelName = m_channels[aChannel - 1].Name;
    }

    if (aLength > kMaxNearMessageSize)
        return SendResult::TooLarge;

    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return SendResult::NotConnected;

    if (!m_nearSupported)
        return SendResult::NotSupported;

    // The server checks this too; checking here saves sending what it would drop
    if (!IsOwnedLocally(aServerId))
        return SendResult::NotOwner;

    // The state may have changed by the time the World update gets to it, in which case the server drops the message
    World::Get().GetRunner().Queue(
        [channelName = std::move(channelName), aServerId, cUnreliable = aDelivery == Delivery::Unreliable, data = String(apBytes ? apBytes : "", apBytes ? aLength : 0)]()
        {
            auto& world = World::Get();
            if (!world.GetTransport().IsOnline())
                return;

            SendAddonNearRequest request{};
            request.Channel = channelName;
            request.ServerId = aServerId;
            request.Unreliable = cUnreliable;
            request.Data = data;
            world.GetTransport().Send(request, cUnreliable ? TiltedPhoques::kUnreliableNoNagle : TiltedPhoques::kReliable);
        });

    return SendResult::Ok;
}

SendResult PluginService::SendToAll(ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept
{
    return Queue(aChannel, ModMessage::kAllPlayers, apBytes, aLength);
}

bool PluginService::IsNearSupported() const noexcept
{
    return m_nearSupported;
}

uint32_t PluginService::GetServerId(uint32_t aFormId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_serverIdsByFormId.find(aFormId);
    return cItor != m_serverIdsByFormId.end() ? cItor->second : 0;
}

uint32_t PluginService::GetFormId(uint32_t aServerId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_actorsByServerId.find(aServerId);
    return cItor != m_actorsByServerId.end() ? cItor->second.FormId : 0;
}

bool PluginService::IsOwnedLocally(uint32_t aServerId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_actorsByServerId.find(aServerId);
    return cItor != m_actorsByServerId.end() && cItor->second.IsLocal;
}

void PluginService::OnNotifyAddonNear(const NotifyAddonNear& acMessage) noexcept
{
    NearMessageCallback onNearMessage = nullptr;
    {
        std::lock_guard cLock(m_lock);

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.Name == acMessage.Channel)
            {
                onNearMessage = cChannel.OnNearMessage;
                break;
            }
        }
    }

    // No plugin takes messages about actors on that channel on this client: nothing to do
    if (onNearMessage)
        onNearMessage(acMessage.SenderPlayerId, acMessage.ServerId, acMessage.Data.data(), acMessage.Data.size());
}

void PluginService::OnNotifyAddonCapabilities(const NotifyAddonCapabilities& acMessage) noexcept
{
    m_nearSupported = acMessage.ApiVersion >= static_cast<uint32_t>(InterfaceVersion::V2) && acMessage.MaxNearMessageSize >= kMaxNearMessageSize;
    spdlog::info("Plugin API: the server relays messages about actors: {}", m_nearSupported.load() ? "yes" : "no");
}

void PluginService::OnConnected(const ConnectedEvent&) noexcept
{
    m_nearSupported = false;

    // A server without Plugin API V2 cannot parse this and drops it, so SendNear stays off there
    AddonHelloRequest request{};
    request.ApiVersion = static_cast<uint32_t>(InterfaceVersion::V2);
    World::Get().GetTransport().Send(request);
}

void PluginService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_nearSupported = false;

    std::lock_guard cLock(m_idLock);
    m_actorsByServerId.clear();
    m_serverIdsByFormId.clear();
}

void PluginService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_anyNearChannel || !World::Get().GetTransport().IsOnline())
        return;

    Map<uint32_t, ActorIds> actorsByServerId;
    Map<uint32_t, uint32_t> serverIdsByFormId;

    auto& world = World::Get();
    const auto localView = world.view<LocalComponent, FormIdComponent>();
    for (const auto cEntity : localView)
    {
        const uint32_t cServerId = localView.get<LocalComponent>(cEntity).Id;
        const uint32_t cFormId = localView.get<FormIdComponent>(cEntity).Id;
        actorsByServerId[cServerId] = {cFormId, true};
        serverIdsByFormId[cFormId] = cServerId;
    }

    const auto remoteView = world.view<RemoteComponent, FormIdComponent>();
    for (const auto cEntity : remoteView)
    {
        const uint32_t cServerId = remoteView.get<RemoteComponent>(cEntity).Id;
        const uint32_t cFormId = remoteView.get<FormIdComponent>(cEntity).Id;
        actorsByServerId[cServerId] = {cFormId, false};
        serverIdsByFormId[cFormId] = cServerId;
    }

    std::lock_guard cLock(m_idLock);
    m_actorsByServerId = std::move(actorsByServerId);
    m_serverIdsByFormId = std::move(serverIdsByFormId);
}
