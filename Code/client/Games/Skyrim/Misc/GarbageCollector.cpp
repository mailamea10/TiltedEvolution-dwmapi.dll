#include <TiltedOnlinePCH.h>

#include <Misc/GarbageCollector.h>

GarbageCollector* GarbageCollector::Get() noexcept
{
    // Deliberately not POINTER_SKYRIMSE: VersionDbPtr answers an unmapped id
    // with the shared zero-returning stub, so the upstream `*s_singleton.Get()`
    // would dereference address 0 on a runtime whose map lacks this data id.
    // Null lets the caller keep the old temporary base alive instead. (The
    // 1.5.97 map carries it as a manual entry.)
    auto* ppSingleton = static_cast<GarbageCollector**>(VersionDb::Get().FindAddressById(400329));
    if (!ppSingleton)
        return nullptr;

    return *ppSingleton;
}

void GarbageCollector::Add(TESBoundObject* apObject) noexcept
{
    // The base-object overload used by Actor::RecalcLeveledActor (among other
    // functions). On 1.5.97 it is SE id 35493, mapped manually.
    TP_THIS_FUNCTION(TAdd, void, GarbageCollector, TESBoundObject*);
    POINTER_SKYRIMSE(TAdd, s_add, 36460);
    TiltedPhoques::ThisCall(s_add, this, apObject);
}
