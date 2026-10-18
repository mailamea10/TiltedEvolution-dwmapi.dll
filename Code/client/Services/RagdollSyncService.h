#pragma once

#include <Structs/RagdollPose.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyRagdollPose;
struct Actor;

/**
 * @brief Makes a knocked down, dying or dead actor's ragdoll look the same on every screen.
 *
 * STR does not run the physics of actors another client owns (their actor process is skipped), so a copy that
 * is knocked down has no ragdoll and lies in the skeleton's rest pose. Running the physics on every client
 * would not help either: each simulation would land the body somewhere else.
 *
 * So the owner's simulation is the only one. While an actor the client owns is ragdolled, the world transforms
 * of the skeleton bones that carry its ragdoll bodies are sampled as the renderer begins each frame (after the
 * frame's animation, physics and ragdoll updates) and streamed, on the same timeline as its movement. The other
 * clients play them back at the copy's interpolated render time and, at that same point of the frame so that
 * nothing overwrites them before they are drawn, put each of those bones exactly where the owner has it,
 * filling in the bones between them from the copy's own pose. A body at rest keeps its pose, also for players
 * who arrive later; when the actor gets up, the copy blends back to its animation.
 */
struct RagdollSyncService
{
    RagdollSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~RagdollSyncService() noexcept = default;

    TP_NOCOPYMOVE(RagdollSyncService);

    // Whether the actor's own simulation has it in a ragdoll (knocked down or out, dying, dead, unconscious).
    static bool IsRagdolled(const Actor* apActor) noexcept;

    // Called by the renderer once the frame is drawn; reports whether the poses put on as it began held.
    static void OnFrameEnd() noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnNotifyPose(const NotifyRagdollPose& acMessage) noexcept;

private:
    struct Snapshot
    {
        uint64_t Tick{};
        uint8_t Flags{};
        Vector<RagdollPose::Bone> Bones{};
    };

    // An actor this client owns.
    struct LocalRagdoll
    {
        uint32_t FormId{};
        bool Active{};
        // Taken over from the previous owner rather than simulated here (a body at rest whose owner left).
        bool Inherited{};
        Vector<RagdollPose::Bone> LastSent{};
        double SinceSend{};
        double StillFor{};
        bool RestSent{};
    };

    // An actor another client owns.
    struct RemoteRagdoll
    {
        Vector<Snapshot> Snapshots{};
        bool Ended{};
        float Weight{};
        double SinceArrival{};
        bool LoggedApply{};
    };

    void UpdateLocal(double aDelta) noexcept;
    void UpdateRemote(double aDelta) noexcept;
    void Send(Vector<RagdollPose>& aStreaming, Vector<RagdollPose>& aHeld) noexcept;

    World& m_world;
    TransportService& m_transport;

    Map<uint32_t, LocalRagdoll> m_local;
    Map<uint32_t, RemoteRagdoll> m_remote;
    double m_sendTimer{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_notifyConnection;
};
