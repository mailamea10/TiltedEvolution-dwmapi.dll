#include "SubtitleManager.h"
#include "MenuTopicManager.h"

#include <Events/SubtitleEvent.h>

#include <TESObjectREFR.h>
#include <Games/ActorExtension.h>
#include <Forms/TESQuest.h>

#include <Forms/TESTopicInfo.h>
#include <Misc/BSFixedString.h>

SubtitleManager* SubtitleManager::Get() noexcept
{
    POINTER_SKYRIMSE(SubtitleManager*, s_singleton, 400443);
    return *s_singleton.Get();
}

TP_THIS_FUNCTION(TShowSubtitle, void, SubtitleManager, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aIsInDialogue);
static TShowSubtitle* RealShowSubtitle = nullptr;

void SubtitleManager::ShowSubtitle(TESObjectREFR* apSpeaker, const char* apSubtitleText, TESTopicInfo* apTopicInfo, bool aUnk1) noexcept
{
    TiltedPhoques::ThisCall(RealShowSubtitle, this, apSpeaker, apSubtitleText, aUnk1);
}

void* SubtitleManager::HideSubtitle(TESObjectREFR* apSpeaker) noexcept
{
    TP_THIS_FUNCTION(THideSubtitle, void*, SubtitleManager, TESObjectREFR* apSpeaker);
    POINTER_SKYRIMSE(THideSubtitle, s_hideSubtitle, 52627);
    return TiltedPhoques::ThisCall(s_hideSubtitle, this, apSpeaker);
}

void TP_MAKE_THISCALL(HookShowSubtitle, SubtitleManager, TESObjectREFR* apSpeaker, const char* apSubtitleText, bool aIsInDialogue)
{
    Actor* pActor = Cast<Actor>(apSpeaker);
    const bool isNpc = pActor && !pActor->GetExtension()->IsPlayer();
    const bool isPlayerDialogue = isNpc && MenuTopicManager::IsPlayerDialogueSpeaker(pActor);
    const bool shouldSyncSubtitle = apSubtitleText && isNpc && (pActor->GetExtension()->IsLocal() || isPlayerDialogue);
    if (shouldSyncSubtitle)
    {
        // Same as dialogue: clients that play the same scene show their own subtitle for the line
        BGSScene* pScene = isPlayerDialogue ? nullptr : pActor->GetCurrentScene();

        SubtitleEvent event(apSpeaker->formID, apSubtitleText);
        if (pScene && pScene->isPlaying)
        {
            event.SceneID = pScene->formID;
            event.ScenePhase = pScene->currentPhaseIndex;
        }
        World::Get().GetRunner().Trigger(event);
    }

    TiltedPhoques::ThisCall(RealShowSubtitle, apThis, apSpeaker, apSubtitleText, aIsInDialogue);
}

void SubtitleManager::InstallHooks() noexcept
{
    // Not installed with the startup hooks: SKSE plugins load after those, and some (po3's Floating
    // Subtitles) hook this prologue by copying its first 5 bytes verbatim, which breaks on our jump.
    // Hooked after them, MinHook relocates their jump instead and both hooks run.
    static bool s_installed = false;
    if (s_installed)
        return;
    s_installed = true;

    TP_HOOK_IMMEDIATE(&RealShowSubtitle, HookShowSubtitle);
}

static TiltedPhoques::Initializer s_subtitleHooks(
    []()
    {
        POINTER_SKYRIMSE(TShowSubtitle, s_showSubtitle, 52626);

        // Usable before InstallHooks; it then points at the trampoline.
        RealShowSubtitle = s_showSubtitle.Get();
    });
