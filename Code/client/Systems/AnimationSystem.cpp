#include <TiltedOnlinePCH.h>

#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Messages/RequestActionEvents.h>
#include <Structs/Movement.h>

#include <Components.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Magic/ActorMagicCaster.h>
#include <CastProbe.h>
#include <Games/ActorExtension.h>
#include <Forms/MagicItem.h>
#include <intrin.h>

namespace
{
// How long a refused magic hand action is retried before it is dropped. Long enough to cover a draw or
// equip transition, short enough that the actions queued behind it are not visibly late.
constexpr uint64_t kMagicRetryWindowMs = 400;

// Behavior events of the casting hands: MRh_ (right), MLh_/MLH_ (left) and DualMagic_ (both).
bool IsMagicHandEvent(std::string_view aEventName) noexcept
{
    const auto startsWith = [aEventName](std::string_view aPrefix)
    {
        return aEventName.size() >= aPrefix.size() && _strnicmp(aEventName.data(), aPrefix.data(), aPrefix.size()) == 0;
    };
    return startsWith("MRh_") || startsWith("MLh_") || startsWith("DualMagic_");
}

// ---- Remote casting probe (temporary) -------------------------------------------------------------
// The remote copy of a casting player often stands with its arms down while the owner's flames come out
// of its hand, even though every replayed casting action was accepted. Something on the remote side takes
// the graph back out of the cast. This logs, for remote players only, the casting related events that
// reach the behavior graph and which game function sent them, so one test session shows the culprit.
using TGraphNotify = bool(void* apHolder, BSFixedString* apEvent);
TGraphNotify* s_realGraphNotify = nullptr;
ptrdiff_t s_graphHolderOffset = 0;
void** s_probedVTable = nullptr;

bool IsCastingRelatedEvent(std::string_view aName) noexcept
{
    // moveStart/moveStop arrive every frame while walking and would use up the line budget.
    if (aName.size() >= 4 && _strnicmp(aName.data(), "move", 4) == 0)
        return false;

    static constexpr std::string_view kKeys[] = {"cast", "spell", "mrh", "mlh", "magic", "equip", "draw", "sheath", "interrupt", "dual", "stop"};
    for (auto key : kKeys)
    {
        if (aName.size() < key.size())
            continue;
        for (size_t i = 0; i + key.size() <= aName.size(); ++i)
        {
            if (_strnicmp(aName.data() + i, key.data(), key.size()) == 0)
                return true;
        }
    }
    return false;
}

uintptr_t GameRva(const void* apAddress) noexcept
{
    return reinterpret_cast<uintptr_t>(apAddress) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

bool HookGraphNotify(void* apHolder, BSFixedString* apEvent)
{
    const bool cResult = s_realGraphNotify(apHolder, apEvent);

    auto* pActor = reinterpret_cast<Actor*>(static_cast<uint8_t*>(apHolder) - s_graphHolderOffset);
    if (apEvent && apEvent->data && pActor->GetExtension()->IsRemotePlayer() && IsCastingRelatedEvent(apEvent->data))
    {
        static uint32_t s_logged = 0;
        if (s_logged < 600)
        {
            ++s_logged;
            CastProbeLog("graph event '{}' on {:X} -> {} (sent from SkyrimSE+{:X}, hands L{:X} R{:X})", apEvent->data, pActor->formID, cResult,
                         GameRva(_ReturnAddress()), pActor->magicItems[0] ? pActor->magicItems[0]->formID : 0u,
                         pActor->magicItems[1] ? pActor->magicItems[1]->formID : 0u);
        }
    }

    return cResult;
}

void InstallGraphProbe(Actor* apActor) noexcept
{
    if (s_probedVTable)
        return;

    auto* pHolder = &apActor->animationGraphHolder;
    auto** pVTable = *reinterpret_cast<void***>(pHolder);
    s_graphHolderOffset = reinterpret_cast<uint8_t*>(pHolder) - reinterpret_cast<uint8_t*>(apActor);

    constexpr size_t kNotifySlot = 1;
    DWORD oldProtect = 0;
    if (!VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    s_realGraphNotify = reinterpret_cast<TGraphNotify*>(pVTable[kNotifySlot]);
    pVTable[kNotifySlot] = reinterpret_cast<void*>(&HookGraphNotify);
    VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), oldProtect, &oldProtect);

    s_probedVTable = pVTable;
    CastProbeLog("watching graph events of remote players (holder at +{:X}, notify was SkyrimSE+{:X})", s_graphHolderOffset,
                 GameRva(reinterpret_cast<void*>(s_realGraphNotify)));
}
} // namespace

void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    if (apActor->GetExtension()->IsRemotePlayer() && apActor->formType == FormType::Character)
        InstallGraphProbe(apActor);

    {
        // Shows whether remote actors are updated at all, and why the probe would not have been installed.
        static uint64_t s_lastBeat = 0;
        const uint64_t cNow = GetTickCount64();
        if (cNow - s_lastBeat > 10000)
        {
            s_lastBeat = cNow;
            CastProbeLog("heartbeat: updating remote {:X}, remote player {}, form type {}, queued actions {}, probe installed {}", apActor->formID,
                         apActor->GetExtension()->IsRemotePlayer(), static_cast<int>(apActor->formType), aAnimationComponent.TimePoints.size(),
                         s_probedVTable != nullptr);
        }
    }

    const auto it = std::begin(actions);
    if (it != std::end(actions) && it->Tick <= aTick)
    {
        // Check if animation graph is ready before attempting to play animations
        if (!apActor->animationGraphHolder.IsReady())
        {
            // Animation graph not ready, keep the action in queue and try again later
            return;
        }
        if (aAnimationComponent.ReplayCount > 0 && aAnimationComponent.ResetAnimationGraphForReplay)
        {
            apActor->animationGraphHolder.RevertAnimationGraphManager();
            aAnimationComponent.ResetAnimationGraphForReplay = false;
        }

        const auto& first = *it;

        const auto actionId = first.ActionId;
        const auto targetId = first.TargetId;

        const auto pAction = Cast<BGSAction>(TESForm::GetById(actionId));
        const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(targetId));

        apActor->actorState.flags1 = first.State1;
        apActor->actorState.flags2 = first.State2;

        apActor->LoadAnimationVariables(first.Variables);

        aAnimationComponent.LastRanAction = first;

        // Play the animation
        TESActionData actionData(first.Type & 0x3, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(first.EventName.c_str());
        actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(first.IdleId));
        actionData.someFlag = ((first.Type & 0x4) != 0) ? 1 : 0;

        bool result = ActorMediator::Get()->ForceAction(&actionData);
        const std::string_view cEventName = first.EventName.c_str();
        const bool cMagicHandEvent = IsMagicHandEvent(cEventName);
        bool sentToGraph = false;

        // ForceAction plays the owner's idle through the actor's AI, which refuses it while the remote copy
        // is still finishing an idle or transition the owner had already left. The behavior graph itself
        // usually takes the event at that point, so hand it the event directly, as SendAnimEvent would.
        if (!result && cMagicHandEvent)
        {
            BSFixedString eventName(first.EventName.c_str());
            sentToGraph = apActor->SendAnimationEvent(&eventName);
            eventName.Release();
            result = sentToGraph;
        }

        // Still refused: the graph is mid-transition. Dropping the action leaves the hands idle for the whole
        // cast, so keep it at the head of the queue and try again on the next frames for a short while.
        // Replays rebuild state in one go and are never held up.
        const uint64_t cNowMs = GetTickCount64();
        if (!result && cMagicHandEvent && aAnimationComponent.ReplayCount == 0)
        {
            if (aAnimationComponent.MagicRetryStartMs == 0)
                aAnimationComponent.MagicRetryStartMs = cNowMs;

            if (cNowMs - aAnimationComponent.MagicRetryStartMs < kMagicRetryWindowMs)
            {
                ++aAnimationComponent.MagicRetryAttempts;
                return;
            }
        }

        if (cMagicHandEvent && (!result || sentToGraph || aAnimationComponent.MagicRetryAttempts > 0))
        {
            static uint32_t s_logged = 0;
            if (s_logged < 60)
            {
                ++s_logged;
                spdlog::info("remote magic action '{}' on {:X}: {} (idle refused{}, {} retries over {} ms)", first.EventName.c_str(), apActor->formID,
                             result ? "played" : "dropped", sentToGraph ? ", graph took the event" : "", aAnimationComponent.MagicRetryAttempts,
                             aAnimationComponent.MagicRetryStartMs ? cNowMs - aAnimationComponent.MagicRetryStartMs : 0);
            }
        }

        if (cMagicHandEvent && apActor->GetExtension()->IsRemotePlayer())
        {
            static uint32_t s_replayed = 0;
            if (s_replayed < 300)
            {
                ++s_replayed;
                CastProbeLog("replayed '{}' on {:X} -> {} (owner hands state2 {:X}, remote hands L{:X} R{:X})", first.EventName.c_str(), apActor->formID,
                             result, first.State2, apActor->magicItems[0] ? apActor->magicItems[0]->formID : 0u,
                             apActor->magicItems[1] ? apActor->magicItems[1]->formID : 0u);
            }
        }

        aAnimationComponent.MagicRetryStartMs = 0;
        aAnimationComponent.MagicRetryAttempts = 0;

        {
            if (!result || cMagicHandEvent || cEventName.find("Magic") != std::string_view::npos || cEventName.find("raw") != std::string_view::npos)
            {
                const auto casterState = [apActor](int aSlot) -> int
                {
                    auto* pCaster = apActor->casters[aSlot];
                    return pCaster ? static_cast<int>(pCaster->eState) : -1;
                };
                spdlog::debug("[magicdiag] remote {:X} action {:X} event '{}' -> {} (state1 {:X} state2 {:X} idle {:X} type {} casters L{} R{})", apActor->formID,
                             actionId, first.EventName.c_str(), result, first.State1, first.State2, first.IdleId, first.Type, casterState(0), casterState(1));
            }
        }

        if (aAnimationComponent.ReplayCount > 0)
            aAnimationComponent.ReplayCount--;

        actions.pop_front();
    }
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<RemoteAnimationComponent>(aEntity))
        aWorld.remove<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::AddActionsForReplay(RemoteAnimationComponent& aAnimationComponent,
                                          const ActionReplayChain& acReplay) noexcept
{
    // A replay rebuilds past state, so it is due right away rather than when playback reaches the original ticks
    for (auto action : acReplay.Actions)
    {
        action.Tick = 0;
        aAnimationComponent.TimePoints.push_back(std::move(action));
    }

    aAnimationComponent.ReplayCount = acReplay.Actions.size();
    aAnimationComponent.ResetAnimationGraphForReplay = acReplay.ResetAnimationGraph;
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto itor = std::begin(aAnimationComponent.TimePoints);
    const auto end = std::cend(aAnimationComponent.TimePoints);

    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::SerializeMovement(World& aWorld, Actor* apActor, Movement& aMovement) noexcept
{
    if (const auto pCell = apActor->parentCell)
        aWorld.GetModSystem().GetServerModId(pCell->formID, aMovement.CellId.ModId, aMovement.CellId.BaseId);

    if (const auto pWorldSpace = apActor->GetWorldSpace())
        aWorld.GetModSystem().GetServerModId(pWorldSpace->formID, aMovement.WorldSpaceId.ModId, aMovement.WorldSpaceId.BaseId);

    aMovement.Position = apActor->position;

    aMovement.Rotation.x = apActor->rotation.x;
    aMovement.Rotation.y = apActor->rotation.z;

    apActor->SaveAnimationVariables(aMovement.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        aMovement.Direction = apActor->currentProcess->middleProcess->direction;
    }
}

void AnimationSystem::SerializeActions(RequestActionEvents& aMessage, LocalComponent& localComponent, LocalAnimationComponent& animationComponent) noexcept
{
    if (animationComponent.Actions.empty())
        return;

    localComponent.CurrentAction = animationComponent.Actions.back();

    aMessage.Actions[localComponent.Id] = std::move(animationComponent.Actions);
    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 14];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}
