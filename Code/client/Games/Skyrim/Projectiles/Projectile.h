#pragma once

#include <Games/Skyrim/TESObjectREFR.h>
#include <Games/Magic/MagicSystem.h>

struct BGSProjectile;
struct TESObjectREFR;
struct TESObjectWEAP;
struct TESAmmo;
struct TESObjectCELL;
struct MagicItem;
struct AlchemyItem;
struct CombatController;
struct TESForm;

struct Projectile : TESObjectREFR
{
    struct LaunchData
    {
        virtual ~LaunchData() = default;

        // When a player is shooting an arrow, all remaining uknown fields are null
        NiPoint3 Origin;
        NiPoint3 ContactNormal;   // always null init? not syncing it now, idk if it matters
        TESForm* pProjectileBase; // is actually BGSProjectile*
        TESObjectREFR* pShooter;
        CombatController* pCombatController;
        TESObjectWEAP* pFromWeapon;
        TESAmmo* pFromAmmo;
        float fZAngle;
        float fXAngle;
        float fYAngle;
        uint8_t unk54[0x14]; // seemingly always null
        TESObjectCELL* pParentCell;
        MagicItem* pSpell;
        MagicSystem::CastingSource eCastingSource;
        bool bUnkBool1;
        uint8_t unk7D[0xB]; // seemingly always null
        AlchemyItem* pPoison;
        int32_t iArea;               // set to 0 in TESObjectWEAP::Fire()
        float fPower;                // init to 1.0 and set to 1.0 before launch in TESObjectWEAP::Fire()
        float fScale;                // init to 1.0 and set to 1.0 before launch in TESObjectWEAP::Fire()
        bool bAlwaysHit;             // usually false
        bool bNoDamageOutsideCombat; // usually false
        bool bAutoAim;               // usually true (probably not bAutoAim then?)
        bool bUnkBool2;              // usually false
        bool bUseOrigin;             // usually false, should be set true for us
        bool bDeferInitialization;   // usually false
        bool bForceConeOfFire;       // unsure // usually false
    };

    // Projectile's own data starts right after TESObjectREFR, which is 8 bytes smaller on
    // 1.5.x, so every field below sits 8 bytes earlier there (fPower: 0x190 on 1.6.x,
    // 0x188 on 1.5.97). Fixed absolute offsets made 1.5.97 write the power into the
    // range field, so remote spell projectiles expired as soon as they were launched.
    uint8_t unkA0[0x80];
    void* pActorCause;
    uint32_t hShooter;
    uint8_t unk[0x190 - 0x12C];
    float fPower;
    float fSpeedMult;
    float fRange;
    float fAge;
    float fDamage;

    static BSPointerHandle<Projectile>* Launch(BSPointerHandle<Projectile>* apResult, LaunchData& arData) noexcept;
};

static_assert(sizeof(Projectile::LaunchData) == 0xA8);
static_assert(offsetof(Projectile, fPower) == sizeof(TESObjectREFR) + 0xF0);
#ifndef SKYRIM_TARGET_LEGACY
static_assert(offsetof(Projectile, fPower) == 0x190);
#endif
