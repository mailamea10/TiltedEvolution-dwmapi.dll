#pragma once

struct TESObjectREFR;

struct SubtitleManager
{
    static SubtitleManager* Get() noexcept;
    // Installs the ShowSubtitle hook once. Called from the first frame, after SKSE plugins have loaded.
    static void InstallHooks() noexcept;

    // TODO(cosideci): confirm usage of bool
    void ShowSubtitle(TESObjectREFR* apSpeaker, const char* apSubtitleText, TESTopicInfo* apTopicInfo, bool aIsInDialogue = false) noexcept;
    void* HideSubtitle(TESObjectREFR* apSpeaker) noexcept;
};
