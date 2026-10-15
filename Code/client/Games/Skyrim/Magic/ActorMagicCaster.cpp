#include "ActorMagicCaster.h"

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>

#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

#include <World.h>

#include <atomic>

TP_THIS_FUNCTION(TSpellCast, void, ActorMagicCaster, bool sbSuccess, int32_t auiTargetCount, MagicItem* apSpell);
TP_THIS_FUNCTION(TInterruptCast, void, ActorMagicCaster, bool);

TP_THIS_FUNCTION(TCheckCast, uint64_t, ActorMagicCaster, MagicItem* apSpell, bool abDualCast, float* apEffectStrength,
                 MagicSystem::CannotCastReason* apReason, bool abUseBaseMagicka);

static TSpellCast* RealSpellCast = nullptr;
static TInterruptCast* RealInterruptCast = nullptr;
static TCheckCast* RealCheckCast = nullptr;

// What the address library said the two functions are, before hooking moved the pointers to trampolines.
static void* s_spellCastTarget = nullptr;
static void* s_interruptCastTarget = nullptr;

void TP_MAKE_THISCALL(HookSpellCast, ActorMagicCaster, bool abSuccess, int32_t auiTargetCount, MagicItem* apSpell)
{
    spdlog::debug("[magicdiag] HookSpellCast actor {:X} success {} targets {} spell {:X} current {:X} source {}",
                 apThis->pCasterActor ? apThis->pCasterActor->formID : 0u, abSuccess, auiTargetCount, apSpell ? apSpell->formID : 0u,
                 apThis->pCurrentSpell ? apThis->pCurrentSpell->formID : 0u, static_cast<int>(apThis->eCastingSource));

    // Note: these if guards is how the game does it too
    if (!apThis->pCasterActor)
        return;
    if (!abSuccess)
        return;
    if (!apSpell && !apThis->pCurrentSpell)
        return;

    if (apThis->pCasterActor->GetExtension()->IsRemote())
        return;

    uint32_t targetFormId = 0;

    if (apThis->hDesiredTarget)
    {
        TESObjectREFR* pDesiredTarget = TESObjectREFR::GetByHandle(apThis->hDesiredTarget.handle.iBits);
        if (pDesiredTarget)
        {
            targetFormId = pDesiredTarget->formID;
        }
    }

    if (apSpell)
        World::Get().GetRunner().Trigger(SpellCastEvent(apThis, apSpell->formID, targetFormId));

    TiltedPhoques::ThisCall(RealSpellCast, apThis, abSuccess, auiTargetCount, apSpell);
}

void TP_MAKE_THISCALL(HookInterruptCast, ActorMagicCaster, bool abRefund)
{
    ActorExtension* pExtended = apThis->pCasterActor->GetExtension();

    spdlog::debug("[magicdiag] HookInterruptCast actor {:X} local {} source {}", apThis->pCasterActor->formID, pExtended->IsLocal(),
                 static_cast<int>(apThis->eCastingSource));

    if (pExtended->IsLocal())
        World::Get().GetRunner().Trigger(InterruptCastEvent(apThis->pCasterActor->formID, apThis->eCastingSource));

    TiltedPhoques::ThisCall(RealInterruptCast, apThis, abRefund);
}

// A remote player's actor replays the owner's casting actions, but the game still checks the cast against the
// remote copy's magicka. That magicka is the owner's synced value, which the owner's own cast has already
// spent, so the replayed cast is charged a second time against what is left. Once it no longer covers the
// cost the game refuses the cast and interrupts it: the effect, sound and projectile still arrive through
// spell and projectile sync, but the hands never play the cast. The owner already paid for the cast, so a
// remote actor's cast is never refused for magicka.
uint64_t TP_MAKE_THISCALL(HookCheckCast, ActorMagicCaster, MagicItem* apSpell, bool abDualCast, float* apEffectStrength,
                          MagicSystem::CannotCastReason* apReason, bool abUseBaseMagicka)
{
    const uint64_t cResult = TiltedPhoques::ThisCall(RealCheckCast, apThis, apSpell, abDualCast, apEffectStrength, apReason, abUseBaseMagicka);

    if ((cResult & 0xFF) == 0 && apReason && *apReason == MagicSystem::NOCAST_MAGICKA && apThis->pCasterActor &&
        apThis->pCasterActor->GetExtension()->IsRemote())
    {
        // Logged a few times only, so a test session shows whether this is what blocked remote casts.
        static std::atomic<int> s_logged{0};
        if (s_logged.fetch_add(1) < 10)
            spdlog::info("remote cast by {:X} (spell {:X}) was refused for magicka; allowed, the owner already paid for it",
                         apThis->pCasterActor->formID, apSpell ? apSpell->formID : 0u);

        *apReason = MagicSystem::NOCAST_OK;
        return 1;
    }

    return cResult;
}

static TiltedPhoques::Initializer s_actorMagicCasterHooks(
    []()
    {
        POINTER_SKYRIMSE(TSpellCast, s_spellCast, 34144);
        POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34140);

        RealSpellCast = s_spellCast.Get();
        RealInterruptCast = s_interruptCast.Get();
        s_spellCastTarget = reinterpret_cast<void*>(RealSpellCast);
        s_interruptCastTarget = reinterpret_cast<void*>(RealInterruptCast);

        TP_HOOK(&RealSpellCast, HookSpellCast);
        TP_HOOK(&RealInterruptCast, HookInterruptCast);
    });

namespace
{
uintptr_t ModuleRva(const void* apAddress) noexcept
{
    return reinterpret_cast<uintptr_t>(apAddress) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

// Points a virtual slot of the ActorMagicCaster vtable at our hook and returns the function it held.
void* SwapVirtual(void** apVTable, size_t aSlot, void* apHook) noexcept
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(&apVTable[aSlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return nullptr;

    void* pOriginal = apVTable[aSlot];
    apVTable[aSlot] = apHook;
    VirtualProtect(&apVTable[aSlot], sizeof(void*), oldProtect, &oldProtect);
    return pOriginal;
}
} // namespace

// The spell cast hooks are installed by address library id. On 1.5.97 the id translation can land on
// a neighbouring function, in which case the hook never fires and no concentration spell is synced.
// The vtable of a live caster is the ground truth, so check it once and redirect the slots if needed.
void ActorMagicCaster::VerifyHooks(ActorMagicCaster* apCaster) noexcept
{
    static bool s_verified = false;
    if (s_verified || !apCaster)
        return;
    s_verified = true;

    auto** pVTable = *reinterpret_cast<void***>(apCaster);
    constexpr size_t kInterruptCastSlot = 8;
    constexpr size_t kSpellCastSlot = 9;

    void* pSpellCast = pVTable[kSpellCastSlot];
    void* pInterruptCast = pVTable[kInterruptCastSlot];

    spdlog::debug("[magicdiag] ActorMagicCaster vtable: SpellCast slot {:X} (hooked {:X}), InterruptCast slot {:X} (hooked {:X})", ModuleRva(pSpellCast),
                 ModuleRva(s_spellCastTarget), ModuleRva(pInterruptCast), ModuleRva(s_interruptCastTarget));

    // CheckCast has no hook by id; the vtable slot is the same on every runtime.
    constexpr size_t kCheckCastSlot = 10;
    // The original must be in place before the slot points at the hook, which another thread may call at once.
    RealCheckCast = reinterpret_cast<TCheckCast*>(pVTable[kCheckCastSlot]);
    if (!SwapVirtual(pVTable, kCheckCastSlot, reinterpret_cast<void*>(&HookCheckCast)))
        spdlog::error("ActorMagicCaster::CheckCast could not be hooked; remote casts can be refused for magicka");

    // Only the 1.5.97 id translation is in question; leave the verified 1.6.x hooks alone.
#ifdef SKYRIM_TARGET_LEGACY
    if (pSpellCast != s_spellCastTarget)
    {
        if (void* pOriginal = SwapVirtual(pVTable, kSpellCastSlot, reinterpret_cast<void*>(&HookSpellCast)))
        {
            RealSpellCast = reinterpret_cast<TSpellCast*>(pOriginal);
            spdlog::warn("ActorMagicCaster::SpellCast hook was on the wrong function, redirected through the vtable");
        }
    }

    if (pInterruptCast != s_interruptCastTarget)
    {
        if (void* pOriginal = SwapVirtual(pVTable, kInterruptCastSlot, reinterpret_cast<void*>(&HookInterruptCast)))
        {
            RealInterruptCast = reinterpret_cast<TInterruptCast*>(pOriginal);
            spdlog::warn("ActorMagicCaster::InterruptCast hook was on the wrong function, redirected through the vtable");
        }
    }
#endif
}
