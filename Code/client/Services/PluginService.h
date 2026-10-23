#pragma once

#include <SkyrimTogetherAPI.h>

struct NotifyModMessage;

/**
 * @brief The Plugin API that SKSE plugins use to sync their own state through Skyrim Together.
 *
 * Not a World service, as plugins may ask for the interface before the World exists. The World forwards
 * NotifyModMessage to it.
 */
struct PluginService final : SkyrimTogetherAPI::IVSkyrimTogether1
{
    [[nodiscard]] static PluginService& Get() noexcept;

    TP_NOCOPYMOVE(PluginService);

    SkyrimTogetherAPI::ChannelHandle
    RegisterChannel(const char* apName, SkyrimTogetherAPI::MessageCallback aOnMessage, SkyrimTogetherAPI::PartyChangedCallback aOnPartyChanged) noexcept override;
    SkyrimTogetherAPI::SendResult Send(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendTo(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept override;
    uint32_t GetLocalPlayerId() const noexcept override;
    bool IsInParty() const noexcept override;

    /**
     * @brief Hands a relayed Mod Message to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept;

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
    };

    // Validates on the calling thread, then sends during the World update. aTargetPlayerId 0 means the whole party.
    SkyrimTogetherAPI::SendResult Queue(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept;

    mutable std::mutex m_lock;
    // A handle is the index in here plus one
    Vector<Channel> m_channels;
    // The other party members as of the last change, sorted. Lets any thread check a SendTo target.
    Vector<uint32_t> m_members;
};
