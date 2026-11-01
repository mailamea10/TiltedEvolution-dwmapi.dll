#pragma once

#include <Structs/Tints.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct Actor;

/**
 * @brief The Skyrim Together side of RaceMenu Together (RaceMenuTogether/STRRaceMenuAPI.h).
 *
 * Syncs what RaceMenu adds on top of the vanilla face: sculpting (including heads imported on the sculpt tab),
 * RaceMenu's own sliders, body morphs, node transforms, overlays and the rest of a RaceMenu preset (including
 * one loaded from the preset tab).
 *
 * The owner's appearance is taken with RaceMenu's own preset export when RaceMenu closes (and once after
 * joining), compressed, and sent in pieces as Plugin API V2 messages about the player's character, which the
 * server relays to the players in range and never stores; on a server without V2 appearances are not shared.
 * Every player sends their appearance again when another player comes into range, since the server does not
 * keep it. On the other clients the pieces are put back together
 * and the appearance is put on that player's copy with RaceMenu's own preset loader. Three things RaceMenu's
 * loader does not hold on a copy are looked after here: each copy gets its own hair color form, overlays are
 * set up before the preset's overlay overrides arrive, and body morphs are re-asserted when BodyGen or OBody -
 * which treat a copy like any NPC - put their own morphs on it.
 */
struct RaceMenuTogetherService
{
    RaceMenuTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~RaceMenuTogetherService() noexcept;

    TP_NOCOPYMOVE(RaceMenuTogetherService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

public:
    // A piece of the appearance of player aSenderId. Runs during the World update.
    void OnNearMessage(uint32_t aSenderId, uint32_t aServerId, const char* apBytes, size_t aLength) noexcept;

private:
    struct RemotePreset
    {
        uint32_t Revision{};
        String Json{};
        Tints FaceTints{};

        // What the preset was last put on, so a respawned actor or a reloaded body gets it again.
        uint32_t AppliedFormId{};
        uint32_t AppliedRevision{};
        const void* AppliedRoot{};
        uint32_t FailedAttempts{};

        // Parsed once per revision: the owner's body morphs (morph name, key, value) and whether the preset
        // carries overlay nodes.
        struct BodyMorph
        {
            String Name;
            String Key;
            float Value;
        };
        Vector<BodyMorph> BodyMorphs{};
        bool HasOverlayNodes{};
        uint32_t ParsedRevision{};

        // This copy's own hair color form. RaceMenu edits one color form from RaceMenu.esp in place, and every
        // copy STR spawns from a RaceMenu player points at that same form, so without its own form each copy
        // takes whatever hair color was set last on this machine.
        void* HairColorForm{};
        bool HairColorHeld{};

        // Seconds until the second pass after a load, and until the next body morph check.
        double SettleTimer{-1.0};
        double MorphCheckTimer{};
    };

    // An appearance arriving in pieces.
    struct Incoming
    {
        uint32_t Revision{};
        uint32_t Count{};
        uint32_t Received{};
        Vector<String> Pieces{};
    };

    void ScanForPlugin() noexcept;
    bool RaceMenuAvailable() noexcept;
    void SendLocalPreset(const char* acpReason) noexcept;
    void SendBlob(const String& acBlob, uint32_t aRevision) noexcept;
    void SendQueued(double aDelta) noexcept;
    void WatchForNewPlayers(double aDelta) noexcept;
    void OnBlob(uint32_t aPlayerId, uint32_t aRevision, const String& acBlob) noexcept;
    void ApplyPending(double aDelta) noexcept;
    bool ApplyPreset(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept;
    void ParsePreset(RemotePreset& aPreset) noexcept;
    void EnforceBodyMorphs(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept;
    void EnsureOverlays(uint32_t aPlayerId, Actor* apActor, const RemotePreset& aPreset) noexcept;
    void Reset() noexcept;

    World& m_world;
    TransportService& m_transport;

    // The plugin's settings, once found.
    bool m_pluginFound{false};
    double m_scanTimer{};
    double m_scanElapsed{};
    Vector<String> m_protectedMorphKeys{};

    Map<uint32_t, RemotePreset> m_remotePresets;
    Map<uint32_t, Incoming> m_incoming;

    // Players seen this session; a new one gets everybody's appearance again.
    Set<uint32_t> m_knownPlayers;
    double m_resendTimer{-1.0};

    String m_lastBlob{};
    uint32_t m_lastRevision{0};
    uint32_t m_localRevision{0};

    bool m_wasOnline{false};
    bool m_raceMenuWasOpen{false};
    bool m_sendAfterJoin{false};
    double m_joinDelay{0.0};
    double m_applyTimer{0.0};
    bool m_reportedMissing{false};

    // Pieces waiting to go out, paced under the server's rate limit.
    Vector<String> m_outgoing;
    double m_sendBudget{};
    // Whether this connection's server relays appearances has been logged, and how long it has been connected.
    bool m_relayChecked{};
    double m_connectedFor{};

    entt::scoped_connection m_updateConnection;
};
