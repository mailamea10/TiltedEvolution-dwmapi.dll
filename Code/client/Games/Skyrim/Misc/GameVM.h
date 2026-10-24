#pragma once

#include <Misc/BSScript.h>

struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

    // The vm as the game itself handed it to one of our papyrus hooks. That is
    // the only source that holds on every runtime; the field below is the
    // fallback for callers that get there before any hook has run, so callers
    // still have to cope with getting nothing back.
    static void SetVirtualMachine(BSScript::IVirtualMachine* apVirtualMachine) noexcept;
    static BSScript::IVirtualMachine* GetVirtualMachine() noexcept;

    // Each runtime states its own offsets rather than deriving one from
    // another (docs/PITFALLS.md, section 2):
    // - 1.5.97: virtualMachine at 0x200 (SkyrimVM::Update at 0x921F10 loads
    //   [rsi+0x200] and calls through it), inactive at 0x680, read as 32 bits
    //   as the legacy build always has;
    // - 1.6.1170: virtualMachine at 0x200, inactive at 0x680;
    // - 1.7.104: virtualMachine at 0x210, inactive at 0x690.
    // inactive gates World::Update on every vm tick (SkyrimVM64.cpp), so a
    // wrong offset silently stops the client, connecting included.
#if defined(SKYRIM_TARGET_LEGACY)
    uint8_t pad8[0x200 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad208[0x680 - 0x208];
    int32_t inactive;
#elif defined(TP_SKYRIM_1_6_1170)
    uint8_t pad8[0x200 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad208[0x680 - 0x208];
    uint8_t inactive;
#else
    uint8_t pad8[0x210 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad218[0x690 - 0x218];
    uint8_t inactive;
#endif

    static constexpr int kInactiveOffset =
#if defined(SKYRIM_TARGET_LEGACY) || defined(TP_SKYRIM_1_6_1170)
        0x680;
#else
        0x690;
#endif
};

#if defined(SKYRIM_TARGET_LEGACY) || defined(TP_SKYRIM_1_6_1170)
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x200);
static_assert(offsetof(SkyrimVM, inactive) == 0x680);
#else
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x210);
static_assert(offsetof(SkyrimVM, inactive) == 0x690);
#endif

using GameVM = SkyrimVM;
