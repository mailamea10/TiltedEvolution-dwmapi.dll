#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyScriptAnimation;

// Where a skeleton bone that carries a ragdoll body is, in world space.
struct RagdollTogetherBone
{
    // 16-bit hash of the bone's name; bones are matched across clients by it.
    uint16_t NameHash{};
    float Position[3]{};
    // x, y, z, w
    float Rotation[4]{0.f, 0.f, 0.f, 1.f};
};

/**
 * @brief Makes a knocked down, dying or dead actor's ragdoll look the same on every screen.
 *
 * Skyrim Together does not run the physics of actors another client owns, so a copy that is knocked down has no
 * ragdoll and lies in the skeleton's rest pose. Running the physics on every client would not help either: each
 * simulation would land the body somewhere else.
 *
 * So the owner's simulation is the only one. While an actor the client owns is ragdolled, the world transforms of
 * the skeleton bones that carry its ragdoll bodies are sampled as the renderer begins each frame (after the frame's
 * animation, physics and ragdoll updates) and sent through Skyrim Together's script animation relay, stamped with the
 * owner's time on the same timeline as its movement. The other clients play them back a little behind the copy's
 * movement and, at that same point of the frame so that nothing overwrites them before they are drawn, put each of
 * those bones where the owner has it, filling in the bones between them from the copy's own pose. A body at rest
 * keeps its pose, also for players who arrive later. The owner keeps streaming through the get-up, and once the
 * actor is back on its feet the copy blends back to its animation.
 *
 * Every distinct relayed string stays in the server's string cache for the rest of the session, so poses go out
 * compact and only while a body moves.
 */
struct RagdollTogetherService
{
    RagdollTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~RagdollTogetherService() noexcept = default;

    TP_NOCOPYMOVE(RagdollTogetherService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept;

private:
    using Bone = RagdollTogetherBone;

    struct Snapshot
    {
        uint64_t Tick{};
        uint8_t Flags{};
        Vector<Bone> Bones{};
    };

    struct OutgoingPose
    {
        uint32_t ServerId{};
        uint8_t Flags{};
        Vector<Bone> Bones{};
    };

    // An actor this client owns.
    struct LocalRagdoll
    {
        uint32_t FormId{};
        bool Active{};
        // Taken over from the previous owner rather than simulated here (a body at rest whose owner left).
        bool Inherited{};
        Vector<Bone> LastSent{};
        // The pose a still body is measured against, so that slow movement adds up instead of counting as still.
        Vector<Bone> StillAnchor{};
        double SinceSend{};
        double StillFor{};
        bool RestSent{};
        // Out of the ragdoll (getting up), and back on its feet.
        double OutFor{};
        double NormalFor{};
        double MissingFor{};
    };

    // An actor another client owns.
    struct RemoteRagdoll
    {
        Vector<Snapshot> Snapshots{};
        bool Ended{};
        float Weight{};
        double SinceArrival{};
        // The owner tick shown last; playback never goes back from it.
        double RenderTick{};
    };

    void ScanForPlugin() noexcept;
    void UpdateLocal(double aDelta) noexcept;
    void UpdateRemote(double aDelta) noexcept;
    void Send(Vector<OutgoingPose>& aStreaming, Vector<OutgoingPose>& aHeld, double aSampleAge) noexcept;
    void Reset() noexcept;

    World& m_world;
    TransportService& m_transport;

    bool m_pluginFound{};
    double m_scanTimer{};
    double m_scanElapsed{};

    Map<uint32_t, LocalRagdoll> m_local;
    Map<uint32_t, RemoteRagdoll> m_remote;
    double m_sendTimer{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_notifyConnection;
};
