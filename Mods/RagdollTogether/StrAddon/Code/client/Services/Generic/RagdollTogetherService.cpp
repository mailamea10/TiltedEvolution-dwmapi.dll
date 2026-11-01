#include <Services/RagdollTogetherService.h>

#include <RagdollTogether/STRRagdollAPI.h>

#include <Components.h>
#include <World.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/PluginService.h>
#include <Services/TransportService.h>

#include <Actor.h>
#include <NetImmerse/NiNode.h>

#include <psapi.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>

// Lets the plugin see that this Skyrim Together build carries the add-on.
extern "C" __declspec(dllexport) uint32_t STRRagdoll_HostApiVersion()
{
    return STRRagdoll::kApiVersion;
}

namespace
{
using Bone = RagdollTogetherBone;

// Poses travel as Plugin API V2 messages about the ragdolled actor: the server relays them to the players who have
// that actor in range and stores nothing. Servers without V2 do not get them at all (never the script animation
// relay, whose string cache would keep every pose for the rest of the session).
constexpr const char* kChannelName = "mailamea.RagdollTogether";
constexpr uint8_t kWireVersion = 1;

// 1.8.3 plays remote movement a fixed time behind the server clock, without the playback clock of 1.8.5
// (apply_addon.py --protocol 1.8.3 defines this).
#ifndef STR_RAGDOLL_PROTOCOL_183
#define STR_RAGDOLL_PROTOCOL_183 0
#endif
#if STR_RAGDOLL_PROTOCOL_183
constexpr double kLegacyMovementDelay = 300.0;
#endif

// How long to wait for the server's answer on relaying ragdolls before saying it does not.
constexpr double kRelayAnswerWait = 10.0;

// The plugin can load late, so keep looking for it for a while.
constexpr double kScanInterval = 1.0;
constexpr double kScanDuration = 60.0;

// How often poses go out while a body moves, at an even pace; playback interpolates between them.
constexpr double kSendInterval = 1.0 / 15.0;
// Owned bodies are sampled this often as frames begin, twice the send rate, rather than on every frame.
constexpr double kSampleInterval = 1.0 / 30.0;
// A still body waits this long before it is sent as at rest.
constexpr double kRestAfter = 0.75;
// A body at rest is sent again now and then, for players who come into range or reload it.
constexpr double kRestKeepAliveAlive = 1.0;
constexpr double kRestKeepAliveDead = 5.0;
// After the ragdoll the owner keeps sending what it draws, so the copies show the same get-up; the ragdoll ends once
// the actor has been back on its feet this long (a short break between two knock states does not end it), or after
// the longest get-up.
constexpr double kEndAfterNormal = 0.75;
constexpr double kMaxGetUp = 8.0;
// Warned about once a ragdoll has had no bones for this long (the first frame has not been sampled yet).
constexpr double kMissingWarnAfter = 0.5;
// Below these a body counts as still.
constexpr float kStillDistance = 0.25f;
constexpr float kStillRotationDot = 0.9998f;
// Blending the copy onto the owner's pose, and back to its animation once the ragdoll ends.
constexpr float kBlendInSeconds = 0.12f;
constexpr float kBlendOutSeconds = 0.3f;
// A copy that stops hearing about a ragdoll gives it up after this long (a body at rest: longer, unless dead).
constexpr double kStreamTimeout = 6.0;
constexpr double kRestTimeout = 15.0;
constexpr size_t kMaxSnapshots = 32;
// Never play back further behind the newest pose than this, should the movement clock stall.
constexpr double kMaxPlaybackLag = 500.0;
// Poses play back this far (ms) behind the copy's movement, which is about one movement interval behind its owner:
// a pose interval more, so the next pose has normally arrived. One that is late is extrapolated for a short while.
constexpr double kPlaybackDelay = 70.0;
constexpr double kMaxExtrapolation = 100.0;
// Only between poses this close (ms): across a longer gap the movement is not known well enough.
constexpr double kMaxExtrapolationSpan = 200.0;
// A skeleton has fewer ragdoll bodies than this.
constexpr size_t kMaxBones = 48;
// Offsets from the first bone, in 1/64 game units: +-512 units covers any skeleton.
constexpr float kOffsetScale = 64.f;

enum PoseFlags : uint8_t
{
    // The ragdoll ended (the actor is back on its feet); blend back to the animation.
    kEnd = 1 << 0,
    // The bodies came to rest; this pose holds until the next one.
    kRest = 1 << 1,
};

// The ragdoll bodies of a skeleton hang off its bones as blend collision objects.
constexpr const char* kRagdollCollisionRtti = "bhkBlendCollisionObject";

// NiAVObject: name at 0x10, collision object at 0x40, local transform at 0x48, world transform at 0x7C (the same on
// every runtime this add-on builds for).
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

// The RTTI object of bhkBlendCollisionObject once one has been seen, so later nodes compare a pointer, not a name.
// Only the render thread uses it.
const void* s_pRagdollRtti = nullptr;

bool IsRagdollBone(const NiAVObject* apObject) noexcept
{
    NiObject* pCollision = GetCollisionObject(apObject);
    if (!pCollision)
        return false;
    const auto* pRtti = reinterpret_cast<const RttiView*>(pCollision->GetRTTI());
    if (!pRtti)
        return false;
    if (pRtti == s_pRagdollRtti)
        return true;
    if (s_pRagdollRtti || !pRtti->Name || std::strcmp(pRtti->Name, kRagdollCollisionRtti) != 0)
        return false;
    s_pRagdollRtti = pRtti;
    return true;
}

uint16_t HashName(const char* acpName) noexcept
{
    uint32_t hash = 2166136261u;
    for (const char* p = acpName; p && *p; ++p)
    {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;
    }
    return static_cast<uint16_t>(hash ^ (hash >> 16));
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

// --- the wire: one message per pose, little-endian ---
//   u8 version, u64 owner tick, u8 flags, u8 bone count, then per bone: u16 name hash, the position (the first bone
//   as three f32, the others as three i16 offsets from it in 1/64 units) and the rotation as its three smallest
//   components (u8 index of the largest, three i16). 48 bones come to 731 bytes, under kMaxNearMessageSize.

template <class T> void Put(String& aOut, T aValue) noexcept
{
    char bytes[sizeof(T)];
    std::memcpy(bytes, &aValue, sizeof(T));
    aOut.append(bytes, sizeof(T));
}

template <class T> bool Take(const char* apIn, size_t aLength, size_t& aPosition, T& aValue) noexcept
{
    if (aPosition + sizeof(T) > aLength)
        return false;
    std::memcpy(&aValue, apIn + aPosition, sizeof(T));
    aPosition += sizeof(T);
    return true;
}

int16_t ToInt16(float aValue) noexcept
{
    return static_cast<int16_t>(std::lround(std::clamp(aValue, -32767.f, 32767.f)));
}

// Components other than the largest lie within +-1/sqrt(2).
constexpr float kQuatScale = 32767.f * 1.41421356f;

void PutRotation(String& aOut, const float acQ[4]) noexcept
{
    uint8_t largest = 0;
    for (uint8_t i = 1; i < 4; ++i)
    {
        if (std::fabs(acQ[i]) > std::fabs(acQ[largest]))
            largest = i;
    }
    const float cSign = acQ[largest] < 0.f ? -1.f : 1.f;
    Put<uint8_t>(aOut, largest);
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (i != largest)
            Put<int16_t>(aOut, ToInt16(acQ[i] * cSign * kQuatScale));
    }
}

bool TakeRotation(const char* apIn, size_t aLength, size_t& aPosition, float aQ[4]) noexcept
{
    uint8_t largest = 0;
    if (!Take(apIn, aLength, aPosition, largest) || largest > 3)
        return false;

    float sum = 0.f;
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (i == largest)
            continue;
        int16_t value = 0;
        if (!Take(apIn, aLength, aPosition, value))
            return false;
        aQ[i] = static_cast<float>(value) / kQuatScale;
        sum += aQ[i] * aQ[i];
    }
    aQ[largest] = std::sqrt(std::max(0.f, 1.f - sum));
    return true;
}

// --- state shared with the frame hook; the service and the render thread take s_lock to touch it ---

struct HookTarget
{
    Vector<Bone> Bones;
    float Weight{};
};

std::mutex s_lock;
std::atomic<bool> s_anyWork{false};
std::atomic<bool> s_frameHookInstalled{false};
Set<uint32_t> s_sampleWanted;
Map<uint32_t, Vector<Bone>> s_samples;
// When the samples were taken, so they go out stamped with that time rather than the time they are sent.
std::chrono::steady_clock::time_point s_sampleTime{};
Map<uint32_t, HookTarget> s_targets;

void RefreshAnyWork() noexcept
{
    s_anyWork = !s_sampleWanted.empty() || !s_targets.empty();
}

const Bone* FindBone(const Vector<Bone>& acBones, uint16_t aHash, size_t aHint) noexcept
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

// Walks the skeleton from the 3D root: puts the bones with a target where the target has them (blended by weight)
// and keeps every world transform below consistent, so the frame renders the result whether or not the engine's own
// world update already ran.
void ApplyPose(NiAVObject* apObject, const Transform& acParentWorld, const HookTarget& acTarget, size_t& aBoneIndex, int aDepth) noexcept
{
    if (!apObject || aDepth > 64)
        return;

    Transform& local = Local(apObject);

    if (IsRagdollBone(apObject))
    {
        if (const auto* pBone = FindBone(acTarget.Bones, HashName(GetName(apObject)), aBoneIndex))
        {
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
            ApplyPose(pChild, cWorld, acTarget, aBoneIndex, aDepth + 1);
    }
}

void SamplePose(NiAVObject* apObject, Vector<Bone>& aBones, int aDepth) noexcept
{
    if (!apObject || aDepth > 64 || aBones.size() >= kMaxBones)
        return;

    if (IsRagdollBone(apObject))
    {
        const Transform& world = WorldTransform(apObject);
        Bone bone;
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

// Puts the owner's pose on a copy, or samples the pose of an actor this client owns. Runs with s_lock held.
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
        for (auto* pChild : pRoot->children)
            ApplyPose(pChild, cRootWorld, *apTarget, boneIndex, 1);
    }

    if (aSample)
    {
        Vector<Bone>& bones = s_samples[aFormId];
        bones.clear();
        SamplePose(pRoot, bones, 0);
    }
}

// Runs as the renderer begins a frame, after the frame's animation, physics and ragdoll updates, so whatever the
// bones hold now is what gets drawn. Ragdolls this client owns are sampled here too, so what is sent is what the
// owner sees.
void OnFrameBegin() noexcept
{
    if (!s_anyWork)
        return;

    // Held while the poses are put on, instead of copying every target each frame: the service only takes it for
    // short updates.
    std::lock_guard guard(s_lock);

    const auto cNow = std::chrono::steady_clock::now();
    const bool cSample = !s_sampleWanted.empty() && std::chrono::duration<double>(cNow - s_sampleTime).count() >= kSampleInterval;
    if (cSample)
        s_sampleTime = cNow;

    for (const auto& [formId, target] : s_targets)
        ProcessActor(formId, cSample && s_sampleWanted.count(formId) != 0, &target);
    if (!cSample)
        return;
    for (const uint32_t cFormId : s_sampleWanted)
    {
        if (s_targets.count(cFormId) == 0)
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

uint32_t GetKnockState(const Actor* apActor) noexcept
{
    return (apActor->actorState.flags1 >> 25) & 0x7;
}

bool IsDeadOrDying(const Actor* apActor) noexcept
{
    const uint32_t cLifeState = (apActor->actorState.flags1 >> 21) & 0xF;
    return cLifeState == 1 || cLifeState == 2;
}

// Whether the actor's own simulation has it in a ragdoll. Knock states (actorState.flags1 bits 25-27): 1 and 2 are
// the fall, 3 and 4 a knockout, 6 the get-up that follows (5 is the short wait between the fall and the get-up, 0
// standing). Life states (bits 21-24): 1 dying, 2 dead, 3 unconscious.
bool IsRagdolled(const Actor* apActor) noexcept
{
    const uint32_t cLifeState = (apActor->actorState.flags1 >> 21) & 0xF;
    const uint32_t cKnockState = GetKnockState(apActor);
    const bool cKnocked = cKnockState == 1 || cKnockState == 2 || cKnockState == 3 || cKnockState == 4 || cKnockState == 6;
    const bool cLimp = cLifeState == 1 || cLifeState == 2 || cLifeState == 3;
    return cKnocked || cLimp;
}

bool IsStill(const Vector<Bone>& acNow, const Vector<Bone>& acBefore) noexcept
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

void Interpolate(const Vector<Bone>& acFrom, const Vector<Bone>& acTo, float aT, Vector<Bone>& aOut) noexcept
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

static TiltedPhoques::Initializer s_frameHook(
    []()
    {
        // Inert until the add-on is switched on: the hook returns at once while there is nothing to do.
        if (void* pBegin = VersionDb::Get().FindAddressById(77245))
        {
            RealRendererBegin = reinterpret_cast<TRendererBegin>(pBegin);
            TP_HOOK_IMMEDIATE(&RealRendererBegin, &HookRendererBegin);
            s_frameHookInstalled = true;
        }
    });
// The Plugin API calls back into whichever service is alive; channels last for the whole process.
RagdollTogetherService* s_pService = nullptr;
SkyrimTogetherAPI::ChannelHandle s_channel = 0;

void OnNearMessageThunk(uint32_t aSenderId, uint32_t aServerId, const char* apBytes, size_t aLength)
{
    if (s_pService)
        s_pService->OnNearMessage(aSenderId, aServerId, apBytes, aLength);
}
} // namespace

RagdollTogetherService::RagdollTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&RagdollTogetherService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&RagdollTogetherService::OnDisconnected>(this);

    s_pService = this;
    if (s_channel == 0)
    {
        auto& api = PluginService::Get();
        s_channel = api.RegisterChannel(kChannelName, nullptr, nullptr);
        if (s_channel == 0 || !api.SetNearMessageCallback(s_channel, &OnNearMessageThunk))
            spdlog::error("Ragdoll Together: could not register the '{}' channel; ragdolls are not shared", kChannelName);
    }
}

RagdollTogetherService::~RagdollTogetherService() noexcept
{
    if (s_pService == this)
        s_pService = nullptr;
}

void RagdollTogetherService::ScanForPlugin() noexcept
{
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return;

    const DWORD cCount = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
    for (DWORD i = 0; i < cCount; ++i)
    {
        const auto getDescriptor = reinterpret_cast<STRRagdoll::TGetDescriptor>(GetProcAddress(modules[i], STRRagdoll::kDescriptorExport));
        if (!getDescriptor)
            continue;

        const STRRagdoll::Descriptor* pDescriptor = getDescriptor();
        m_pluginFound = true;
        if (!pDescriptor || pDescriptor->ApiVersion != STRRagdoll::kApiVersion)
        {
            m_pluginFound = false;
            m_scanElapsed = kScanDuration;
            spdlog::warn("Ragdoll Together: the installed RagdollTogether.dll has a descriptor this add-on cannot use (API {}, add-on API {}); "
                         "install the matching Ragdoll Together files",
                         pDescriptor ? pDescriptor->ApiVersion : 0, STRRagdoll::kApiVersion);
            return;
        }

        if (s_frameHookInstalled)
            spdlog::info("Ragdoll Together: {} {} found; ragdolls are shared", pDescriptor->ModName ? pDescriptor->ModName : "Ragdoll Together",
                         pDescriptor->ModVersion ? pDescriptor->ModVersion : "");
        else
            spdlog::warn("Ragdoll Together: the renderer could not be hooked on this game version; ragdolls are not shared");
        return;
    }
}

void RagdollTogetherService::Reset() noexcept
{
    m_local.clear();
    m_remote.clear();

    std::lock_guard guard(s_lock);
    s_sampleWanted.clear();
    s_samples.clear();
    s_targets.clear();
    RefreshAnyWork();
}

void RagdollTogetherService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    Reset();
    m_relayChecked = false;
    m_connectedFor = 0.0;
}

void RagdollTogetherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pluginFound && m_scanElapsed < kScanDuration)
    {
        m_scanElapsed += acEvent.Delta;
        m_scanTimer -= acEvent.Delta;
        if (m_scanTimer <= 0.0)
        {
            m_scanTimer = kScanInterval;
            ScanForPlugin();
        }
    }

    if (!m_pluginFound || !s_frameHookInstalled || !m_transport.IsOnline() || s_channel == 0)
        return;

    // The server answers whether it relays messages about actors shortly after joining; say once what it means.
    if (!PluginService::Get().IsNearSupported())
    {
        m_connectedFor += acEvent.Delta;
        if (!m_relayChecked && m_connectedFor >= kRelayAnswerWait)
        {
            m_relayChecked = true;
            spdlog::warn("Ragdoll Together: this server does not relay ragdolls (it needs an AllTogether server or a Skyrim "
                         "Together server with Plugin API V2); ragdolls are not shared in this session");
        }
        return;
    }
    if (!m_relayChecked)
    {
        m_relayChecked = true;
        spdlog::info("Ragdoll Together: the server relays ragdolls to the players in range");
    }

    UpdateLocal(acEvent.Delta);
    UpdateRemote(acEvent.Delta);
}

void RagdollTogetherService::OnNearMessage(uint32_t, uint32_t aServerId, const char* apBytes, size_t aLength) noexcept
{
    if (!m_pluginFound || !apBytes)
        return;

    size_t position = 0;
    uint8_t version = 0;
    uint64_t tick = 0;
    uint8_t flags = 0;
    uint8_t boneCount = 0;
    if (!Take(apBytes, aLength, position, version) || version != kWireVersion || !Take(apBytes, aLength, position, tick) ||
        !Take(apBytes, aLength, position, flags) || !Take(apBytes, aLength, position, boneCount) || boneCount == 0 || boneCount > kMaxBones)
        return;

    Snapshot snapshot{tick, flags, {}};
    snapshot.Bones.resize(boneCount);
    for (uint8_t b = 0; b < boneCount; ++b)
    {
        Bone& bone = snapshot.Bones[b];
        if (!Take(apBytes, aLength, position, bone.NameHash))
            return;
        if (b == 0)
        {
            for (float& value : bone.Position)
            {
                if (!Take(apBytes, aLength, position, value))
                    return;
            }
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                int16_t offset = 0;
                if (!Take(apBytes, aLength, position, offset))
                    return;
                bone.Position[axis] = snapshot.Bones[0].Position[axis] + static_cast<float>(offset) / kOffsetScale;
            }
        }
        if (!TakeRotation(apBytes, aLength, position, bone.Rotation))
            return;
    }

    // An actor this client does not show, or one it took over since.
    const auto cEntity = Utils::FindEntityByServerId(aServerId);
    if (!cEntity || m_world.try_get<LocalComponent>(*cEntity))
        return;

    auto& remote = m_remote[aServerId];
    remote.SinceArrival = 0.0;

    // A new ragdoll after the last one ended.
    if (remote.Ended && !(flags & kEnd))
    {
        remote.Ended = false;
        remote.Snapshots.clear();
    }

    // Streaming poses can arrive out of order.
    auto it = std::find_if(remote.Snapshots.begin(), remote.Snapshots.end(), [&](const Snapshot& acOther) { return acOther.Tick >= snapshot.Tick; });
    if (it != remote.Snapshots.end() && it->Tick == snapshot.Tick)
        *it = std::move(snapshot);
    else
        remote.Snapshots.insert(it, std::move(snapshot));

    while (remote.Snapshots.size() > kMaxSnapshots)
        remote.Snapshots.erase(remote.Snapshots.begin());
}

void RagdollTogetherService::UpdateLocal(double aDelta) noexcept
{
    // At an even pace: the copies time their playback by the gaps between poses.
    m_sendTimer += aDelta;
    const bool cSendNow = m_sendTimer >= kSendInterval;
    if (cSendNow)
        m_sendTimer = std::min(m_sendTimer - kSendInterval, kSendInterval);

    Vector<OutgoingPose> streaming;
    Vector<OutgoingPose> held;
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
        if (!cRagdolled && m_local.find(cServerId) == m_local.end())
            continue;

        seen.insert(cServerId);
        auto& local = m_local[cServerId];

        if (cRagdolled && !local.Active)
        {
            local = LocalRagdoll{};
            local.FormId = cFormId;
            local.Active = true;

            // A body that lay here as another client's copy keeps the pose its owner gave it; this client's engine
            // never simulated it.
            auto remoteIt = m_remote.find(cServerId);
            if (remoteIt != m_remote.end() && !remoteIt->second.Snapshots.empty() && !remoteIt->second.Ended)
            {
                local.Inherited = true;
                std::lock_guard guard(s_lock);
                s_targets[cFormId] = HookTarget{remoteIt->second.Snapshots.back().Bones, 1.f};
                RefreshAnyWork();
            }
            if (remoteIt != m_remote.end())
                m_remote.erase(remoteIt);

            std::lock_guard guard(s_lock);
            s_sampleWanted.insert(cFormId);
            RefreshAnyWork();
        }

        local.OutFor = cRagdolled ? 0.0 : local.OutFor + aDelta;
        local.NormalFor = !cRagdolled && GetKnockState(pActor) == 0 ? local.NormalFor + aDelta : 0.0;
        const bool cEnded = !cRagdolled && (local.NormalFor >= kEndAfterNormal || local.OutFor >= kMaxGetUp);

        if (!cSendNow)
            continue;

        Vector<Bone> bones;
        {
            std::lock_guard guard(s_lock);
            auto sampleIt = s_samples.find(local.FormId);
            if (sampleIt != s_samples.end())
                bones = sampleIt->second;
        }

        if (cEnded)
        {
            // Back on its feet: the last pose goes out so every copy blends back from the same place.
            OutgoingPose pose{cServerId, kEnd, bones.empty() ? local.LastSent : bones};
            if (!pose.Bones.empty())
                held.push_back(std::move(pose));

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
            const bool cWasWarned = local.MissingFor >= kMissingWarnAfter;
            local.MissingFor += kSendInterval;
            if (!cWasWarned && local.MissingFor >= kMissingWarnAfter)
                spdlog::warn("Ragdoll Together: no ragdoll bones found on {:X}; its ragdoll is not shared", local.FormId);
            local.SinceSend += kSendInterval;
            continue;
        }

        // Every pose goes out while the body moves, however slowly; it counts as still (and is then sent as at rest)
        // only while it stays close to where it was when it stopped. A get-up is never at rest.
        local.SinceSend += kSendInterval;
        if (cRagdolled && IsStill(bones, local.StillAnchor))
        {
            local.StillFor += kSendInterval;
        }
        else
        {
            local.StillAnchor = bones;
            local.StillFor = 0.0;
            local.RestSent = false;
        }

        OutgoingPose pose{cServerId, 0, bones};
        if (local.StillFor >= kRestAfter)
        {
            const double cKeepAlive = IsDeadOrDying(pActor) ? kRestKeepAliveDead : kRestKeepAliveAlive;
            if (!local.RestSent || local.SinceSend >= cKeepAlive)
            {
                local.RestSent = true;
                local.SinceSend = 0.0;
                local.LastSent = bones;
                pose.Flags = kRest;
                held.push_back(std::move(pose));
            }
        }
        else
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

    if (streaming.empty() && held.empty())
        return;

    std::chrono::steady_clock::time_point sampleTime;
    {
        std::lock_guard guard(s_lock);
        sampleTime = s_sampleTime;
    }
    Send(streaming, held, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sampleTime).count());
}

void RagdollTogetherService::Send(Vector<OutgoingPose>& aStreaming, Vector<OutgoingPose>& aHeld, double aSampleAge) noexcept
{
    // Stamped with the owner's time of the frame the poses were taken from, not the time they leave.
    const uint64_t cNow = m_transport.GetClock().GetCurrentTick();
    const uint64_t cAge = static_cast<uint64_t>(std::clamp(aSampleAge, 0.0, 100.0));
    const uint64_t cTick = cNow > cAge ? cNow - cAge : cNow;

    auto& api = PluginService::Get();
    const auto send = [&](const OutgoingPose& acPose, SkyrimTogetherAPI::Delivery aDelivery)
    {
        const size_t cBones = std::min(acPose.Bones.size(), kMaxBones);
        String wire;
        wire.reserve(12 + cBones * 15);
        Put<uint8_t>(wire, kWireVersion);
        Put<uint64_t>(wire, cTick);
        Put<uint8_t>(wire, acPose.Flags);
        Put<uint8_t>(wire, static_cast<uint8_t>(cBones));
        for (size_t b = 0; b < cBones; ++b)
        {
            const auto& bone = acPose.Bones[b];
            Put<uint16_t>(wire, bone.NameHash);
            if (b == 0)
            {
                for (const float cValue : bone.Position)
                    Put<float>(wire, cValue);
            }
            else
            {
                for (int axis = 0; axis < 3; ++axis)
                    Put<int16_t>(wire, ToInt16((bone.Position[axis] - acPose.Bones[0].Position[axis]) * kOffsetScale));
            }
            PutRotation(wire, bone.Rotation);
        }

        const auto cResult = api.SendNear(s_channel, acPose.ServerId, aDelivery, wire.data(), wire.size());
        if (cResult != SkyrimTogetherAPI::SendResult::Ok && cResult != SkyrimTogetherAPI::SendResult::NotOwner)
            spdlog::debug("Ragdoll Together: pose for {:X} not sent ({})", acPose.ServerId, static_cast<int>(cResult));
    };

    // Streaming poses can be lost (the next one replaces them); the ones that end a ragdoll or hold a body at rest
    // must arrive.
    for (const auto& pose : aHeld)
        send(pose, SkyrimTogetherAPI::Delivery::Reliable);
    for (const auto& pose : aStreaming)
        send(pose, SkyrimTogetherAPI::Delivery::Unreliable);
}

void RagdollTogetherService::UpdateRemote(double aDelta) noexcept
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

        // Play back on the copy's movement timeline, so the body and the actor's position agree, a little further
        // behind so that the pose after the one shown has normally arrived. Never back in time, unless the movement
        // clock jumped back a long way.
        double renderTick = static_cast<double>(remote.Snapshots.back().Tick);
#if STR_RAGDOLL_PROTOCOL_183
        const double cMovementTick = static_cast<double>(m_transport.GetClock().GetCurrentTick()) - kLegacyMovementDelay;
        renderTick = std::max<double>(cMovementTick, renderTick - kMaxPlaybackLag);
#else
        if (auto* pInterpolation = m_world.try_get<InterpolationComponent>(entity); pInterpolation && pInterpolation->Clock.IsRunning())
            renderTick = std::max<double>(pInterpolation->Clock.GetRenderTick() - kPlaybackDelay, renderTick - kMaxPlaybackLag);
#endif
        if (renderTick < remote.RenderTick && remote.RenderTick - renderTick < kMaxPlaybackLag)
            renderTick = remote.RenderTick;
        remote.RenderTick = renderTick;

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
        else if (const double cAhead = renderTick - static_cast<double>(pFrom->Tick); cAhead > 0.0 && remote.Snapshots.size() >= 2 && !(pFrom->Flags & (kRest | kEnd)))
        {
            // The next pose is late: keep the bodies going the way they were for a moment instead of stopping them,
            // then hold.
            const Snapshot& before = remote.Snapshots[remote.Snapshots.size() - 2];
            const double cSpan = static_cast<double>(pFrom->Tick) - static_cast<double>(before.Tick);
            if (cSpan > 0.0 && cSpan <= kMaxExtrapolationSpan)
                Interpolate(before.Bones, pFrom->Bones, static_cast<float>(1.0 + std::min(cAhead, kMaxExtrapolation) / cSpan), target.Bones);
            else
                target.Bones = pFrom->Bones;
        }
        else
        {
            target.Bones = pFrom->Bones;
        }

        // The ragdoll ends once playback reaches the owner's end pose; a stream that stopped without one ends after a
        // while, unless the body is at rest on a dead actor.
        const bool cReachedEnd = (pFrom->Flags & kEnd) != 0;
        const bool cResting = (remote.Snapshots.back().Flags & kRest) != 0;
        const bool cTimedOut = remote.SinceArrival > (cResting ? kRestTimeout : kStreamTimeout) && !IsDeadOrDying(pActor);
        if (cReachedEnd || cTimedOut)
            remote.Ended = true;

        const float cRate = static_cast<float>(aDelta) / (remote.Ended ? -kBlendOutSeconds : kBlendInSeconds);
        remote.Weight = std::clamp(remote.Weight + cRate, 0.f, 1.f);
        target.Weight = remote.Weight;

        if (target.Weight > 0.f)
            targets[cFormId] = std::move(target);
    }

    // Done blending out, or the actor left for good.
    for (auto it = m_remote.begin(); it != m_remote.end();)
    {
        const bool cPresent = present.count(it->first) != 0;
        const bool cFinished = it->second.Ended && it->second.Weight <= 0.f;
        const bool cGone = !cPresent && it->second.SinceArrival > kRestTimeout * 4;
        if (!cPresent)
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
