#include <Services/RagdollSyncService.h>

#include <Components.h>
#include <World.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/NotifyRagdollPose.h>
#include <Messages/RagdollPoseRequest.h>

#include <Services/TransportService.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/GamePatch.h>
#include <NetImmerse/NiNode.h>

#include <atomic>
#include <cstring>
#include <cmath>

namespace
{
// How often poses go out while a body moves, and how long a still body waits before it is sent as at rest.
constexpr double kSendInterval = 0.05;
constexpr double kHeartbeatInterval = 0.25;
constexpr double kRestAfter = 0.75;
// A body at rest is sent again now and then, for players who come into range or reload it.
constexpr double kRestKeepAliveAlive = 1.0;
constexpr double kRestKeepAliveDead = 5.0;
// Below these a body counts as still.
constexpr float kStillDistance = 0.25f;
constexpr float kStillRotationDot = 0.9998f;
// Streaming poses per unreliable message (~300 bytes each for a humanoid), so one lost packet loses little.
constexpr size_t kPosesPerMessage = 3;
// Blending the copy onto the owner's pose, and back to its animation once the ragdoll ends.
constexpr float kBlendInSeconds = 0.12f;
constexpr float kBlendOutSeconds = 0.35f;
// A copy that stops hearing about a ragdoll gives it up after this long (a body at rest: longer, unless dead).
constexpr double kStreamTimeout = 6.0;
constexpr double kRestTimeout = 15.0;
constexpr size_t kMaxSnapshots = 32;
// Never play back further behind the newest pose than this, should the movement clock stall.
constexpr double kMaxPlaybackLag = 500.0;

// The ragdoll bodies of a skeleton hang off its bones as blend collision objects.
constexpr const char* kRagdollCollisionRtti = "bhkBlendCollisionObject";

// SE layouts (NiAVObject): name at 0x10, collision object at 0x40, local transform at 0x48, world at 0x7C.
struct Transform
{
    float Rotation[3][3];
    float Position[3];
    float Scale;
};
static_assert(sizeof(Transform) == 0x34);

struct RttiView
{
    const char* Name;
};

const char* GetName(const NiAVObject* apObject) noexcept
{
    return *reinterpret_cast<const char* const*>(reinterpret_cast<const uint8_t*>(apObject) + 0x10);
}

NiObject* GetCollisionObject(const NiAVObject* apObject) noexcept
{
    return *reinterpret_cast<NiObject* const*>(reinterpret_cast<const uint8_t*>(apObject) + 0x40);
}

Transform& Local(NiAVObject* apObject) noexcept
{
    return *reinterpret_cast<Transform*>(reinterpret_cast<uint8_t*>(apObject) + 0x48);
}

Transform& WorldTransform(NiAVObject* apObject) noexcept
{
    return *reinterpret_cast<Transform*>(reinterpret_cast<uint8_t*>(apObject) + 0x7C);
}

NiNode* AsNode(NiAVObject* apObject) noexcept
{
    // NiObject vtable slot 3.
    using TAsNode = NiNode* (*)(NiAVObject*);
    return reinterpret_cast<TAsNode>((*reinterpret_cast<void***>(apObject))[3])(apObject);
}

bool IsRagdollBone(const NiAVObject* apObject) noexcept
{
    NiObject* pCollision = GetCollisionObject(apObject);
    if (!pCollision)
        return false;
    const auto* pRtti = reinterpret_cast<const RttiView*>(pCollision->GetRTTI());
    return pRtti && pRtti->Name && std::strcmp(pRtti->Name, kRagdollCollisionRtti) == 0;
}

uint32_t HashName(const char* acpName) noexcept
{
    uint32_t hash = 2166136261u;
    for (const char* p = acpName; p && *p; ++p)
    {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;
    }
    return hash;
}

// --- rotation math; matrices act on column vectors (Gamebryo), quaternions are x, y, z, w ---

void MatrixToQuat(const float m[3][3], float q[4]) noexcept
{
    const float cTrace = m[0][0] + m[1][1] + m[2][2];
    if (cTrace > 0.f)
    {
        const float s = std::sqrt(cTrace + 1.f) * 2.f;
        q[3] = 0.25f * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
    {
        const float s = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2.f;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    }
    else if (m[1][1] > m[2][2])
    {
        const float s = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2.f;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    }
    else
    {
        const float s = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2.f;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25f * s;
    }

    const float cLength = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i)
        q[i] /= cLength > 1e-6f ? cLength : 1.f;
}

void QuatToMatrix(const float q[4], float m[3][3]) noexcept
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0][0] = 1.f - 2.f * (y * y + z * z);
    m[0][1] = 2.f * (x * y - z * w);
    m[0][2] = 2.f * (x * z + y * w);
    m[1][0] = 2.f * (x * y + z * w);
    m[1][1] = 1.f - 2.f * (x * x + z * z);
    m[1][2] = 2.f * (y * z - x * w);
    m[2][0] = 2.f * (x * z - y * w);
    m[2][1] = 2.f * (y * z + x * w);
    m[2][2] = 1.f - 2.f * (x * x + y * y);
}

float QuatDot(const float a[4], const float b[4]) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

void Slerp(const float a[4], const float b[4], float t, float out[4]) noexcept
{
    float cosTheta = QuatDot(a, b);
    float target[4] = {b[0], b[1], b[2], b[3]};
    if (cosTheta < 0.f)
    {
        cosTheta = -cosTheta;
        for (float& value : target)
            value = -value;
    }

    float wa, wb;
    if (cosTheta > 0.9995f)
    {
        wa = 1.f - t;
        wb = t;
    }
    else
    {
        const float cTheta = std::acos(cosTheta);
        const float cSin = std::sin(cTheta);
        wa = std::sin((1.f - t) * cTheta) / cSin;
        wb = std::sin(t * cTheta) / cSin;
    }

    float length = 0.f;
    for (int i = 0; i < 4; ++i)
    {
        out[i] = wa * a[i] + wb * target[i];
        length += out[i] * out[i];
    }
    length = std::sqrt(length);
    for (int i = 0; i < 4; ++i)
        out[i] /= length > 1e-6f ? length : 1.f;
}

void Multiply(const float a[3][3], const float b[3][3], float out[3][3]) noexcept
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
}

// out = a^T * b
void MultiplyTransposed(const float a[3][3], const float b[3][3], float out[3][3]) noexcept
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r][c] = a[0][r] * b[0][c] + a[1][r] * b[1][c] + a[2][r] * b[2][c];
}

Transform Compose(const Transform& acParent, const Transform& acLocal) noexcept
{
    Transform result;
    Multiply(acParent.Rotation, acLocal.Rotation, result.Rotation);
    for (int r = 0; r < 3; ++r)
    {
        result.Position[r] = acParent.Position[r] + acParent.Scale * (acParent.Rotation[r][0] * acLocal.Position[0] + acParent.Rotation[r][1] * acLocal.Position[1] +
                                                                      acParent.Rotation[r][2] * acLocal.Position[2]);
    }
    result.Scale = acParent.Scale * acLocal.Scale;
    return result;
}

// --- state shared with the frame hook ---

struct HookTarget
{
    Vector<RagdollPose::Bone> Bones;
    float Weight{};
    uint32_t Matched{};
};

std::mutex s_lock;
std::atomic<bool> s_anyWork{false};
std::atomic<uint64_t> s_frameCalls{0};
std::atomic<bool> s_frameHookInstalled{false};
Set<uint32_t> s_sampleWanted;
Map<uint32_t, Vector<RagdollPose::Bone>> s_samples;
Map<uint32_t, HookTarget> s_targets;
// Bones matched on the last apply per actor, for the main thread's log.
Map<uint32_t, uint32_t> s_matched;

// One bone per actor whose pose was put on this frame, checked again once the frame is drawn: whether the pose
// lasted until then is what decides whether the other players see it.
struct HeldCheck
{
    uint32_t BoneHash{};
    float Position[3]{};
};
Map<uint32_t, HeldCheck> s_heldChecks;
Set<uint32_t> s_heldReported;

void RefreshAnyWork() noexcept
{
    s_anyWork = !s_sampleWanted.empty() || !s_targets.empty();
}

const RagdollPose::Bone* FindBone(const Vector<RagdollPose::Bone>& acBones, uint32_t aHash, size_t aHint) noexcept
{
    if (aHint < acBones.size() && acBones[aHint].NameHash == aHash)
        return &acBones[aHint];
    for (const auto& bone : acBones)
    {
        if (bone.NameHash == aHash)
            return &bone;
    }
    return nullptr;
}

// Walks the skeleton from the 3D root: puts the bones with a target where the target has them (blended by
// weight) and keeps every world transform below consistent, so the frame renders the result whether or not the
// engine's own world update already ran.
void ApplyPose(NiAVObject* apObject, const Transform& acParentWorld, const HookTarget& acTarget, size_t& aBoneIndex, uint32_t& aMatched, int aDepth) noexcept
{
    if (!apObject || aDepth > 64)
        return;

    Transform& local = Local(apObject);

    if (IsRagdollBone(apObject))
    {
        const char* pName = GetName(apObject);
        if (const auto* pBone = FindBone(acTarget.Bones, HashName(pName), aBoneIndex))
        {
            ++aMatched;

            // Desired local = parent world^-1 * target world.
            float targetRotation[3][3];
            QuatToMatrix(pBone->Rotation, targetRotation);

            Transform desired = local;
            MultiplyTransposed(acParentWorld.Rotation, targetRotation, desired.Rotation);
            const float cDelta[3] = {pBone->Position[0] - acParentWorld.Position[0], pBone->Position[1] - acParentWorld.Position[1], pBone->Position[2] - acParentWorld.Position[2]};
            const float cInvScale = acParentWorld.Scale != 0.f ? 1.f / acParentWorld.Scale : 1.f;
            for (int c = 0; c < 3; ++c)
                desired.Position[c] = (acParentWorld.Rotation[0][c] * cDelta[0] + acParentWorld.Rotation[1][c] * cDelta[1] + acParentWorld.Rotation[2][c] * cDelta[2]) * cInvScale;

            if (acTarget.Weight >= 0.999f)
            {
                std::memcpy(local.Rotation, desired.Rotation, sizeof(local.Rotation));
                std::memcpy(local.Position, desired.Position, sizeof(local.Position));
            }
            else
            {
                float from[4], to[4], blended[4];
                MatrixToQuat(local.Rotation, from);
                MatrixToQuat(desired.Rotation, to);
                Slerp(from, to, acTarget.Weight, blended);
                QuatToMatrix(blended, local.Rotation);
                for (int c = 0; c < 3; ++c)
                    local.Position[c] += (desired.Position[c] - local.Position[c]) * acTarget.Weight;
            }
        }
        ++aBoneIndex;
    }

    const Transform cWorld = Compose(acParentWorld, local);
    WorldTransform(apObject) = cWorld;

    if (NiNode* pNode = AsNode(apObject))
    {
        for (auto* pChild : pNode->children)
            ApplyPose(pChild, cWorld, acTarget, aBoneIndex, aMatched, aDepth + 1);
    }
}

void SamplePose(NiAVObject* apObject, Vector<RagdollPose::Bone>& aBones, int aDepth) noexcept
{
    if (!apObject || aDepth > 64 || aBones.size() >= RagdollPose::kMaxBones)
        return;

    if (IsRagdollBone(apObject))
    {
        const Transform& world = WorldTransform(apObject);
        RagdollPose::Bone bone;
        bone.NameHash = HashName(GetName(apObject));
        std::memcpy(bone.Position, world.Position, sizeof(bone.Position));
        MatrixToQuat(world.Rotation, bone.Rotation);
        aBones.push_back(bone);
    }

    if (NiNode* pNode = AsNode(apObject))
    {
        for (auto* pChild : pNode->children)
            SamplePose(pChild, aBones, aDepth + 1);
    }
}

NiAVObject* FindRagdollBone(NiAVObject* apObject, uint32_t aHash, int aDepth) noexcept
{
    if (!apObject || aDepth > 64)
        return nullptr;
    if (IsRagdollBone(apObject) && HashName(GetName(apObject)) == aHash)
        return apObject;
    if (NiNode* pNode = AsNode(apObject))
    {
        for (auto* pChild : pNode->children)
        {
            if (NiAVObject* pFound = FindRagdollBone(pChild, aHash, aDepth + 1))
                return pFound;
        }
    }
    return nullptr;
}

// Puts the owner's pose on a copy, or samples the pose of an actor this client owns.
void ProcessActor(uint32_t aFormId, bool aSample, const HookTarget* apTarget) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
        return;

    NiNode* pRoot = pActor->GetNiNode();
    if (!pRoot)
        return;

    if (apTarget && !apTarget->Bones.empty() && apTarget->Weight > 0.f)
    {
        const Transform cRootWorld = WorldTransform(pRoot);
        size_t boneIndex = 0;
        uint32_t matched = 0;
        for (auto* pChild : pRoot->children)
            ApplyPose(pChild, cRootWorld, *apTarget, boneIndex, matched, 1);

        std::lock_guard guard(s_lock);
        s_matched[aFormId] = matched;
        if (matched && apTarget->Weight >= 0.999f && s_heldReported.count(aFormId) == 0)
        {
            const auto& bone = apTarget->Bones.front();
            HeldCheck check;
            check.BoneHash = bone.NameHash;
            std::memcpy(check.Position, bone.Position, sizeof(check.Position));
            s_heldChecks[aFormId] = check;
        }
    }

    if (aSample)
    {
        Vector<RagdollPose::Bone> bones;
        SamplePose(pRoot, bones, 0);
        std::lock_guard guard(s_lock);
        s_samples[aFormId] = std::move(bones);
    }
}

// Runs as the renderer begins a frame, after the frame's animation, physics and ragdoll updates, so whatever the
// bones hold now is what gets drawn. Ragdolls this client owns are sampled here too, so what is sent is what the
// owner sees.
void OnFrameBegin() noexcept
{
    s_frameCalls.fetch_add(1, std::memory_order_relaxed);
    if (!s_anyWork)
        return;

    Map<uint32_t, HookTarget> targets;
    Set<uint32_t> samples;
    {
        std::lock_guard guard(s_lock);
        targets = s_targets;
        samples = s_sampleWanted;
    }

    for (const auto& [formId, target] : targets)
        ProcessActor(formId, samples.count(formId) != 0, &target);
    for (const uint32_t cFormId : samples)
    {
        if (targets.count(cFormId) == 0)
            ProcessActor(cFormId, true, nullptr);
    }
}

// BSGraphics::Renderer::Begin(uint32_t windowId).
using TRendererBegin = void (*)(void* apRenderer, uint32_t aWindowId);
TRendererBegin RealRendererBegin = nullptr;

void HookRendererBegin(void* apRenderer, uint32_t aWindowId)
{
    OnFrameBegin();
    RealRendererBegin(apRenderer, aWindowId);
}

bool IsDeadOrDying(const Actor* apActor) noexcept
{
    const uint32_t cLifeState = (apActor->actorState.flags1 >> 21) & 0xF;
    return cLifeState == 1 || cLifeState == 2;
}

bool IsStill(const Vector<RagdollPose::Bone>& acNow, const Vector<RagdollPose::Bone>& acBefore) noexcept
{
    if (acNow.size() != acBefore.size())
        return false;
    for (size_t i = 0; i < acNow.size(); ++i)
    {
        const auto& a = acNow[i];
        const auto& b = acBefore[i];
        if (a.NameHash != b.NameHash)
            return false;
        const float dx = a.Position[0] - b.Position[0], dy = a.Position[1] - b.Position[1], dz = a.Position[2] - b.Position[2];
        if (dx * dx + dy * dy + dz * dz > kStillDistance * kStillDistance)
            return false;
        if (std::fabs(QuatDot(a.Rotation, b.Rotation)) < kStillRotationDot)
            return false;
    }
    return true;
}

void Interpolate(const Vector<RagdollPose::Bone>& acFrom, const Vector<RagdollPose::Bone>& acTo, float aT, Vector<RagdollPose::Bone>& aOut) noexcept
{
    aOut = acTo;
    for (size_t i = 0; i < aOut.size(); ++i)
    {
        const auto* pFrom = FindBone(acFrom, aOut[i].NameHash, i);
        if (!pFrom)
            continue;
        for (int c = 0; c < 3; ++c)
            aOut[i].Position[c] = pFrom->Position[c] + (acTo[i].Position[c] - pFrom->Position[c]) * aT;
        Slerp(pFrom->Rotation, acTo[i].Rotation, aT, aOut[i].Rotation);
    }
}
} // namespace

RagdollSyncService::RagdollSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&RagdollSyncService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&RagdollSyncService::OnDisconnected>(this);
    m_notifyConnection = aDispatcher.sink<NotifyRagdollPose>().connect<&RagdollSyncService::OnNotifyPose>(this);
}

bool RagdollSyncService::IsRagdolled(const Actor* apActor) noexcept
{
    if (!apActor)
        return false;

    // ActorState flags1: life state in bits 21-24, knock state in bits 25-27.
    const uint32_t cLifeState = (apActor->actorState.flags1 >> 21) & 0xF;
    const uint32_t cKnockState = (apActor->actorState.flags1 >> 25) & 0x7;

    // Knock: explode, explode lead-in, out, out lead-in, down. Not queued (not down yet) or getting up.
    const bool cKnocked = cKnockState == 1 || cKnockState == 2 || cKnockState == 3 || cKnockState == 4 || cKnockState == 6;
    // Life: dying, dead, unconscious.
    const bool cLimp = cLifeState == 1 || cLifeState == 2 || cLifeState == 3;
    return cKnocked || cLimp;
}

void RagdollSyncService::OnFrameEnd() noexcept
{
    // Reported once per actor: whether the pose put on as the frame began was still there when it was drawn. If
    // something overwrote it, the other players would still see the copy in its rest pose.
    Map<uint32_t, HeldCheck> checks;
    {
        std::lock_guard guard(s_lock);
        if (s_heldChecks.empty())
            return;
        checks.swap(s_heldChecks);
    }

    for (const auto& [formId, check] : checks)
    {
        auto* pActor = Cast<Actor>(TESForm::GetById(formId));
        NiNode* pRoot = pActor ? pActor->GetNiNode() : nullptr;
        NiAVObject* pBone = pRoot ? FindRagdollBone(pRoot, check.BoneHash, 0) : nullptr;
        if (!pBone)
            continue;

        const Transform& world = WorldTransform(pBone);
        const float dx = world.Position[0] - check.Position[0], dy = world.Position[1] - check.Position[1], dz = world.Position[2] - check.Position[2];
        const float cOff = std::sqrt(dx * dx + dy * dy + dz * dz);

        {
            std::lock_guard guard(s_lock);
            s_heldReported.insert(formId);
        }
        if (cOff < 1.f)
            spdlog::info("ragdoll sync: the owner's pose on {:X} held until the frame was drawn", formId);
        else
            spdlog::warn("ragdoll sync: the owner's pose on {:X} was overwritten before the frame was drawn ({:.0f} units off)", formId, cOff);
    }
}

void RagdollSyncService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_local.clear();
    m_remote.clear();

    std::lock_guard guard(s_lock);
    s_sampleWanted.clear();
    s_samples.clear();
    s_targets.clear();
    s_matched.clear();
    s_heldChecks.clear();
    s_heldReported.clear();
    RefreshAnyWork();
}

void RagdollSyncService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    // Once, to tell from a log whether the frame hook runs. Long frames (a loading screen) do not count.
    static double s_watched = 0.0;
    static bool s_reported = false;
    if (!s_reported && acEvent.Delta < 1.0 && (s_watched += acEvent.Delta) > 20.0)
    {
        s_reported = true;
        if (s_frameHookInstalled)
            spdlog::info("ragdoll sync: {} frames seen in the first 20 s of play", s_frameCalls.load());
        else
            spdlog::warn("ragdoll sync: the frame hook is not installed; ragdolls are not shared");
    }

    if (!m_transport.IsConnected())
        return;

    UpdateLocal(acEvent.Delta);
    UpdateRemote(acEvent.Delta);
}

void RagdollSyncService::OnNotifyPose(const NotifyRagdollPose& acMessage) noexcept
{
    for (const auto& pose : acMessage.Poses)
    {
        if (pose.Bones.empty())
            continue;

        auto& remote = m_remote[pose.ServerId];
        remote.SinceArrival = 0.0;

        // A new ragdoll after the last one ended.
        if (remote.Ended && !(pose.PoseFlags & RagdollPose::kEnd))
        {
            remote.Ended = false;
            remote.Snapshots.clear();
            remote.LoggedApply = false;

            std::lock_guard guard(s_lock);
            s_heldReported.clear();
        }

        Snapshot snapshot{acMessage.Tick, pose.PoseFlags, pose.Bones};

        // Unreliable poses can arrive out of order.
        auto it = std::find_if(remote.Snapshots.begin(), remote.Snapshots.end(), [&](const Snapshot& acOther) { return acOther.Tick >= snapshot.Tick; });
        if (it != remote.Snapshots.end() && it->Tick == snapshot.Tick)
            *it = std::move(snapshot);
        else
            remote.Snapshots.insert(it, std::move(snapshot));

        while (remote.Snapshots.size() > kMaxSnapshots)
            remote.Snapshots.erase(remote.Snapshots.begin());
    }
}

void RagdollSyncService::UpdateLocal(double aDelta) noexcept
{
    m_sendTimer += aDelta;
    const bool cSendNow = m_sendTimer >= kSendInterval;
    if (cSendNow)
        m_sendTimer = 0.0;

    Vector<RagdollPose> streaming;
    Vector<RagdollPose> held;
    Set<uint32_t> seen;

    auto view = m_world.view<LocalComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const uint32_t cServerId = view.get<LocalComponent>(entity).Id;
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        if (!pActor || !pActor->GetNiNode())
            continue;

        const bool cRagdolled = IsRagdolled(pActor);
        auto existing = m_local.find(cServerId);
        if (!cRagdolled && existing == m_local.end())
            continue;

        seen.insert(cServerId);
        auto& local = m_local[cServerId];

        if (cRagdolled && !local.Active)
        {
            local = LocalRagdoll{};
            local.FormId = cFormId;
            local.Active = true;

            // A body that lay here as another client's copy keeps the pose its owner gave it; this client's
            // engine never simulated it.
            auto remoteIt = m_remote.find(cServerId);
            if (remoteIt != m_remote.end() && !remoteIt->second.Snapshots.empty() && !remoteIt->second.Ended)
            {
                local.Inherited = true;
                std::lock_guard guard(s_lock);
                s_targets[cFormId] = HookTarget{remoteIt->second.Snapshots.back().Bones, 1.f};
                RefreshAnyWork();
                spdlog::info("ragdoll sync: took over the resting pose of {:X} from its previous owner", cFormId);
            }
            if (remoteIt != m_remote.end())
                m_remote.erase(remoteIt);

            std::lock_guard guard(s_lock);
            s_sampleWanted.insert(cFormId);
            RefreshAnyWork();
        }

        if (!cSendNow)
            continue;

        Vector<RagdollPose::Bone> bones;
        {
            std::lock_guard guard(s_lock);
            auto sampleIt = s_samples.find(local.FormId);
            if (sampleIt != s_samples.end())
                bones = sampleIt->second;
        }

        if (!cRagdolled)
        {
            // Getting up: the last pose goes out reliably so every copy blends back from the same place.
            RagdollPose pose;
            pose.ServerId = cServerId;
            pose.PoseFlags = RagdollPose::kEnd;
            pose.Bones = bones.empty() ? local.LastSent : bones;
            if (!pose.Bones.empty())
                held.push_back(std::move(pose));

            spdlog::info("ragdoll sync: {:X} left its ragdoll", local.FormId);

            std::lock_guard guard(s_lock);
            s_sampleWanted.erase(local.FormId);
            s_samples.erase(local.FormId);
            s_targets.erase(local.FormId);
            RefreshAnyWork();
            m_local.erase(cServerId);
            continue;
        }

        if (bones.empty())
        {
            if (local.SinceSend == 0.0)
                spdlog::warn("ragdoll sync: no ragdoll bones found on {:X}; its ragdoll is not shared", local.FormId);
            local.SinceSend += kSendInterval;
            continue;
        }

        local.SinceSend += kSendInterval;
        const bool cStill = IsStill(bones, local.LastSent);
        local.StillFor = cStill ? local.StillFor + kSendInterval : 0.0;
        if (!cStill)
            local.RestSent = false;

        RagdollPose pose;
        pose.ServerId = cServerId;
        pose.Bones = bones;

        if (local.LastSent.empty())
            spdlog::info("ragdoll sync: sharing the ragdoll of {:X} ({} bodies)", local.FormId, bones.size());

        if (local.StillFor >= kRestAfter)
        {
            const double cKeepAlive = IsDeadOrDying(pActor) ? kRestKeepAliveDead : kRestKeepAliveAlive;
            if (!local.RestSent || local.SinceSend >= cKeepAlive)
            {
                local.RestSent = true;
                local.SinceSend = 0.0;
                local.LastSent = bones;
                pose.PoseFlags = RagdollPose::kRest;
                held.push_back(std::move(pose));
            }
        }
        else if (!cStill || local.SinceSend >= kHeartbeatInterval)
        {
            local.SinceSend = 0.0;
            local.LastSent = bones;
            streaming.push_back(std::move(pose));
        }
    }

    // Actors no longer owned here: whoever owns them now carries on.
    for (auto it = m_local.begin(); it != m_local.end();)
    {
        if (seen.count(it->first) != 0)
        {
            ++it;
            continue;
        }

        std::lock_guard guard(s_lock);
        s_sampleWanted.erase(it->second.FormId);
        s_samples.erase(it->second.FormId);
        if (it->second.Inherited)
            s_targets.erase(it->second.FormId);
        RefreshAnyWork();
        it = m_local.erase(it);
    }

    Send(streaming, held);
}

void RagdollSyncService::Send(Vector<RagdollPose>& aStreaming, Vector<RagdollPose>& aHeld) noexcept
{
    const uint64_t cTick = m_transport.GetClock().GetCurrentTick();

    if (!aHeld.empty())
    {
        RagdollPoseRequest request;
        request.Tick = cTick;
        request.Poses = std::move(aHeld);
        m_transport.Send(request);
    }

    for (size_t i = 0; i < aStreaming.size(); i += kPosesPerMessage)
    {
        RagdollPoseRequest request;
        request.Tick = cTick;
        for (size_t j = i; j < std::min(aStreaming.size(), i + kPosesPerMessage); ++j)
            request.Poses.push_back(std::move(aStreaming[j]));
        m_transport.Send(request, TiltedPhoques::kUnreliableNoNagle);
    }
}

void RagdollSyncService::UpdateRemote(double aDelta) noexcept
{
    if (m_remote.empty())
        return;

    Map<uint32_t, HookTarget> targets;
    Set<uint32_t> present;

    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const uint32_t cServerId = view.get<RemoteComponent>(entity).Id;
        auto it = m_remote.find(cServerId);
        if (it == m_remote.end())
            continue;

        auto& remote = it.value(); // hopscotch_map: second is read-only
        present.insert(cServerId);
        remote.SinceArrival += aDelta;

        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        if (!pActor || remote.Snapshots.empty())
            continue;

        // Play back on the copy's movement timeline, so the body and the actor's position agree.
        double renderTick = static_cast<double>(remote.Snapshots.back().Tick);
        if (auto* pInterpolation = m_world.try_get<InterpolationComponent>(entity); pInterpolation && pInterpolation->Clock.IsRunning())
            renderTick = std::max(pInterpolation->Clock.GetRenderTick(), renderTick - kMaxPlaybackLag);

        const Snapshot* pFrom = &remote.Snapshots.front();
        const Snapshot* pTo = pFrom;
        for (const auto& snapshot : remote.Snapshots)
        {
            if (static_cast<double>(snapshot.Tick) <= renderTick)
                pFrom = &snapshot;
            pTo = &snapshot;
            if (static_cast<double>(snapshot.Tick) > renderTick)
                break;
        }

        HookTarget target;
        if (pTo != pFrom && pTo->Tick > pFrom->Tick)
        {
            const float cT = static_cast<float>(std::clamp((renderTick - static_cast<double>(pFrom->Tick)) / static_cast<double>(pTo->Tick - pFrom->Tick), 0.0, 1.0));
            Interpolate(pFrom->Bones, pTo->Bones, cT, target.Bones);
        }
        else
        {
            target.Bones = pFrom->Bones;
        }

        // The ragdoll ends once playback reaches the owner's end pose; a stream that stopped without one ends
        // after a while, unless the body is at rest on a dead actor.
        const bool cReachedEnd = (pFrom->Flags & RagdollPose::kEnd) != 0;
        const bool cResting = (remote.Snapshots.back().Flags & RagdollPose::kRest) != 0;
        const bool cTimedOut = remote.SinceArrival > (cResting ? kRestTimeout : kStreamTimeout) && !IsDeadOrDying(pActor);
        if ((cReachedEnd || cTimedOut) && !remote.Ended)
        {
            remote.Ended = true;
            spdlog::info("ragdoll sync: the ragdoll of {:X} ended{}, blending back", cFormId, cTimedOut ? " (no word from its owner)" : "");
        }

        const float cRate = static_cast<float>(aDelta) / (remote.Ended ? -kBlendOutSeconds : kBlendInSeconds);
        remote.Weight = std::clamp(remote.Weight + cRate, 0.f, 1.f);
        target.Weight = remote.Weight;

        if (target.Weight > 0.f)
            targets[cFormId] = std::move(target);

        if (!remote.LoggedApply)
        {
            std::lock_guard guard(s_lock);
            auto matched = s_matched.find(cFormId);
            if (matched != s_matched.end())
            {
                remote.LoggedApply = true;
                spdlog::info("ragdoll sync: showing the owner's ragdoll on {:X} ({} of {} bodies matched)", cFormId, matched->second, pFrom->Bones.size());
            }
        }
    }

    // Done blending out, or the actor left for good.
    for (auto it = m_remote.begin(); it != m_remote.end();)
    {
        const bool cFinished = it->second.Ended && it->second.Weight <= 0.f;
        const bool cGone = !(present.count(it->first) != 0) && it->second.SinceArrival > kRestTimeout * 4;
        if (!(present.count(it->first) != 0))
            it.value().SinceArrival += aDelta;
        it = (cFinished || cGone) ? m_remote.erase(it) : std::next(it);
    }

    std::lock_guard guard(s_lock);
    // Keep the targets of bodies this client inherited.
    for (const auto& [serverId, local] : m_local)
    {
        if (!local.Inherited)
            continue;
        auto it = s_targets.find(local.FormId);
        if (it != s_targets.end())
            targets[local.FormId] = it->second;
    }
    s_targets = std::move(targets);
    RefreshAnyWork();
}

static TiltedPhoques::Initializer s_ragdollFrameHook(
    []()
    {
        if (auto* pBegin = GamePatch::Anchor(77245, "ragdoll sync frame begin"))
        {
            RealRendererBegin = reinterpret_cast<TRendererBegin>(pBegin);
            TP_HOOK_IMMEDIATE(&RealRendererBegin, &HookRendererBegin);
            s_frameHookInstalled = true;
        }
    });
