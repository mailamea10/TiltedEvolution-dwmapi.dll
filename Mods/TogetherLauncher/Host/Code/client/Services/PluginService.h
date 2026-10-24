#pragma once

#include <SkyrimTogetherAPI.h>

struct NotifyModMessage;
struct NotifyAddonCapabilities;
struct NotifyAddonNear;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;

/**
 * @brief The Plugin API that SKSE plugins use to sync their own state through Skyrim Together.
 *
 * Not a World service, as plugins may ask for the interface before the World exists. The World forwards
 * NotifyModMessage, NotifyAddonNear and the connection events to it.
 */
struct PluginService final : SkyrimTogetherAPI::IVSkyrimTogether2
{
    [[nodiscard]] static PluginService& Get() noexcept;

    TP_NOCOPYMOVE(PluginService);

    SkyrimTogetherAPI::ChannelHandle
    RegisterChannel(const char* apName, SkyrimTogetherAPI::MessageCallback aOnMessage, SkyrimTogetherAPI::PartyChangedCallback aOnPartyChanged) noexcept override;
    SkyrimTogetherAPI::SendResult Send(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendTo(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept override;
    uint32_t GetLocalPlayerId() const noexcept override;
    bool IsInParty() const noexcept override;

    bool SetNearMessageCallback(SkyrimTogetherAPI::ChannelHandle aChannel, SkyrimTogetherAPI::NearMessageCallback aOnNearMessage) noexcept override;
    SkyrimTogetherAPI::SendResult
    SendNear(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aServerId, SkyrimTogetherAPI::Delivery aDelivery, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendToAll(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    bool IsNearSupported() const noexcept override;
    uint32_t GetServerId(uint32_t aFormId) const noexcept override;
    uint32_t GetFormId(uint32_t aServerId) const noexcept override;
    bool IsOwnedLocally(uint32_t aServerId) const noexcept override;

    /**
     * @brief Hands a relayed Mod Message to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept;

    /**
     * @brief Hands a relayed message about an actor to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyAddonNear(const NotifyAddonNear& acMessage) noexcept;

    /**
     * @brief The server's answer to the hello sent on connecting: whether it relays SendNear.
     */
    void OnNotifyAddonCapabilities(const NotifyAddonCapabilities& acMessage) noexcept;

    /**
     * @brief Asks the server which Plugin API features it relays.
     */
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    /**
     * @brief Refreshes the actor IDs that any thread can look up, while a plugin uses messages about actors.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    /**
     * @brief Any event after which PartyService's member list may have changed. Runs during the World update.
     */
    template <class T> void OnPartyEvent(const T&) noexcept { NotifyPartyChanges(); }

private:
    PluginService() = default;

    // Compares the party members with the last ones seen and tells every channel about the difference
    void NotifyPartyChanges() noexcept;

    struct Channel
    {
        String Name;
        SkyrimTogetherAPI::MessageCallback OnMessage;
        SkyrimTogetherAPI::PartyChangedCallback OnPartyChanged;
        SkyrimTogetherAPI::NearMessageCallback OnNearMessage{nullptr};
    };

    struct ActorIds
    {
        uint32_t FormId;
        bool IsLocal;
    };

    // Validates on the calling thread, then sends during the World update. aTargetPlayerId 0 means the whole party,
    // ModMessage::kAllPlayers every player on the server.
    SkyrimTogetherAPI::SendResult Queue(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept;

    mutable std::mutex m_lock;
    // A handle is the index in here plus one
    Vector<Channel> m_channels;
    // The other party members as of the last change, sorted. Lets any thread check a SendTo target.
    Vector<uint32_t> m_members;
    // Whether any channel takes messages about actors, so the IDs below are worth keeping up to date
    std::atomic<bool> m_anyNearChannel{false};
    std::atomic<bool> m_nearSupported{false};

    mutable std::mutex m_idLock;
    // The synced actors as of the last frame, both ways
    Map<uint32_t, ActorIds> m_actorsByServerId;
    Map<uint32_t, uint32_t> m_serverIdsByFormId;
};
