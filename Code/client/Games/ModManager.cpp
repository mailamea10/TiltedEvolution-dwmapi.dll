#include <TiltedOnlinePCH.h>

#include <Games/TES.h>

#include <Actor.h>
#include <Forms/TESBoundObject.h>

ModManager* ModManager::Get() noexcept
{
    POINTER_SKYRIMSE(ModManager*, modManager, 400269);

    return *modManager.Get();
}

uint32_t Mod::GetFormId(uint32_t aBaseId) const noexcept
{
    if (IsLite())
    {
        aBaseId &= 0xFFF;
        aBaseId |= 0xFE000000;
        aBaseId |= uint32_t(liteId) << 12;
    }
    else
    {
        aBaseId &= 0xFFFFFF;
        aBaseId |= uint32_t(standardId) << 24;
    }

    return aBaseId;
}

TP_THIS_FUNCTION(TSpawnNewREFR, uint32_t&, ModManager, uint32_t& aRefHandleOut, TESForm* apBaseForm, NiPoint3* apPosition, NiPoint3* apRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apActor, uintptr_t a9, uintptr_t a10, char aForcePersist, char a12);
TSpawnNewREFR* RealSpawnNewREFR;

uint32_t& TP_MAKE_THISCALL(SpawnNewREFR, ModManager, uint32_t& aRefHandleOut, TESForm* apBaseForm, NiPoint3* apPosition, NiPoint3* apRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apActor, uintptr_t a9, uintptr_t a10, char aForcePersist, char a12)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    return TiltedPhoques::ThisCall(RealSpawnNewREFR, apThis, aRefHandleOut, apBaseForm, apPosition, apRotation, apParentCell, apWorldSpace, apActor, a9, a10, aForcePersist, a12);
}

uint32_t ModManager::Spawn(NiPoint3& aPosition, NiPoint3& aRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apCharacter) noexcept
{
    uint32_t refrHandle = 0;

    TiltedPhoques::ThisCall(RealSpawnNewREFR, this, refrHandle, apCharacter->baseForm, &aPosition, &aRotation, apParentCell, apWorldSpace, apCharacter, 0, 0, static_cast<char>(0), static_cast<char>(1));

    return refrHandle;
}

// Places a new temporary reference the way the engine places a dropped item.
// For an exterior, pass the worldspace and no cell; the engine picks the cell from the position.
TESObjectREFR* ModManager::SpawnReference(TESBoundObject* apBaseForm, NiPoint3& aPosition, NiPoint3& aRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace) noexcept
{
    uint32_t refrHandle = 0;

    TiltedPhoques::ThisCall(RealSpawnNewREFR, this, refrHandle, apBaseForm, &aPosition, &aRotation, apParentCell, apWorldSpace, nullptr, 0, 0, static_cast<char>(0), static_cast<char>(1));

    if (!refrHandle)
        return nullptr;

    return TESObjectREFR::GetByHandle(refrHandle);
}

Vector<Mod*> ModManager::GetLoadedMods() const noexcept
{
    Vector<Mod*> result;

    // The compiled arrays hold exactly the plugins in the load order. The raw file list can carry
    // extra entries (seen on 1.5.97 with large load orders: ~1700 nameless entries that hid real
    // plugins), so it is only the fallback.
    const bool arraysLookValid = loadedMods.data && loadedMods.length > 0 && loadedMods.length <= 0xFF &&
                                 loadedLiteMods.length <= 0x1000 && (loadedLiteMods.length == 0 || loadedLiteMods.data);

    if (arraysLookValid)
    {
        for (uint32_t i = 0; i < loadedMods.length; ++i)
            if (auto* pMod = loadedMods.data[i])
                result.push_back(pMod);

        for (uint32_t i = 0; i < loadedLiteMods.length; ++i)
            if (auto* pMod = loadedLiteMods.data[i])
                result.push_back(pMod);

        return result;
    }

    for (auto* pMod : const_cast<GameList<Mod>&>(mods))
    {
        if (pMod && pMod->IsLoaded())
            result.push_back(pMod);
    }

    return result;
}

Mod* ModManager::GetByName(const char* acpName) const noexcept
{
    for (auto* pMod : GetLoadedMods())
    {
        if (_stricmp(acpName, pMod->filename) == 0)
            return pMod;
    }

    auto pEntry = &mods.entry;

    while (pEntry && pEntry->data)
    {
        if (_stricmp(acpName, pEntry->data->filename) == 0)
            return pEntry->data;

        pEntry = pEntry->next;
    }

    return nullptr;
}

TESObjectCELL* ModManager::GetCellFromCoordinates(int32_t aX, int32_t aY, TESWorldSpace* aWorldSpace, bool aSpawnCell) noexcept
{
    TP_THIS_FUNCTION(TModManager, TESObjectCELL*, ModManager, int32_t, int32_t, TESWorldSpace*, bool);
    POINTER_SKYRIMSE(TModManager, getCell, 13718);

    return TiltedPhoques::ThisCall(getCell, this, aX, aY, aWorldSpace, aSpawnCell);
}

static TiltedPhoques::Initializer s_tesHooks(
    []()
    {
        POINTER_SKYRIMSE(TSpawnNewREFR, s_realSpawnNewREFR, 13723);

        RealSpawnNewREFR = s_realSpawnNewREFR.Get();

        // TP_HOOK(&RealSpawnNewREFR, SpawnNewREFR);
    });
