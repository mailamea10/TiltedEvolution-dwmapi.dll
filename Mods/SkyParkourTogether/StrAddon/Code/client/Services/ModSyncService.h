#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct NotifyScriptAnimation;

/**
 * @brief The Skyrim Together side of the STR Mod Sync API (ModSync/STRModSyncAPI.h).
 *
 * Finds SKSE plugins that export a mod sync descriptor, watches the local player's animation graph for the
 * events they register, and relays each one (with the graph variables it reads) through the script animation
 * relay every 1.8.5 server already has, so no server change is needed. On the other clients the variables are
 * set on that player's copy and the event is played there.
 */
struct ModSyncService
{
    ModSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~ModSyncService() noexcept = default;

    TP_NOCOPYMOVE(ModSyncService);

    // Called from the player's graph notify hook, possibly off the main thread.
    static void Capture(const char* acpEvent) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept;

private:
    void ScanForPlugins() noexcept;
    void InstallHook() noexcept;
    void SendCaptured() noexcept;

    World& m_world;
    TransportService& m_transport;

    double m_scanTimer{};
    double m_scanElapsed{};
    bool m_hookInstalled{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_notifyConnection;
};
