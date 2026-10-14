// Copyright (C) 2021 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#include <cstdint>
#include <limits>
#include <BuildInfo.h>

#define CLIENT_DLL 0

struct TargetConfig
{
    const wchar_t* dllClientName;
    const wchar_t* fullGameName;
    uint32_t steamAppId;
    uint32_t exeLoadSz;
    // Needs to be kept up to date.
    uint32_t exeDiskSz;
};

// clang-format off

static constexpr TargetConfig CurrentTarget{
    L"SkyrimTogether.dll",
    L"Skyrim Special Edition",
    489830, 0x40000000, 35410264};
#define TARGET_NAME L"SkyrimSE"
#define TARGET_NAME_A "SkyrimSE"
#define PRODUCT_NAME L"Skyrim Together"
#define SHORT_NAME L"Skyrim Special Edition"
// Registry key under Software\TiltedPhoques\TiltedEvolution that remembers the
// selected game. The 1.5.x launcher keeps its own key so it never picks up, or
// overwrites, the install remembered by the regular (1.6) launcher.
#ifdef SKYRIM_TARGET_LEGACY
#define TILTED_REGISTRY_NAME L"Skyrim Special Edition 1.5"
#else
#define TILTED_REGISTRY_NAME SHORT_NAME
#endif

// clang-format on
