
#include <Camera/PlayerCamera.h>
#include <NetImmerse/NiCamera.h>
#include <TiltedOnlinePCH.h>
#include <Games/Skyrim/RttiScan.h>

PlayerCamera* PlayerCamera::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCamera*, s_instance, 400802);
    return *(s_instance.Get());
}

bool PlayerCamera::IsFirstPerson() noexcept
{
#ifdef SKYRIM_TARGET_LEGACY
    // The 1.6 build calls a game helper (AE id 21600) that has no 1.5.x entry in
    // the id map. The answer is the same as asking whether the active camera
    // state is the first person one, so compare its vtable against
    // FirstPersonState's, found through the game's RTTI.
    static void** s_pFirstPersonVTable = RttiScan::FindVTable(".?AVFirstPersonState@@");
    auto* pState = static_cast<TESCamera*>(this)->state;
    return s_pFirstPersonVTable && pState && *reinterpret_cast<void***>(pState) == s_pFirstPersonVTable;
#else
    TP_THIS_FUNCTION(TIsFirstPerson, void, PlayerCamera, void*, void*, double*);
    POINTER_SKYRIMSE(TIsFirstPerson, isFirstPerson, 21600);

    double firstPerson = 0.0;
    TiltedPhoques::ThisCall(isFirstPerson, this, nullptr, nullptr, &firstPerson);

    return firstPerson == 1.0;
#endif
}

bool PlayerCamera::WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance /* = 1e-5f */)
{
    auto* pCam = GetNiCamera();
    if (cameraNode && pCam)
    {
        return pCam->WorldPtToScreenPt3(in, out, zeroTolerance);
    }

    return false;
}

void PlayerCamera::ForceFirstPerson() noexcept
{
    TP_THIS_FUNCTION(TForceFirstPerson, void, PlayerCamera);
    POINTER_SKYRIMSE(TForceFirstPerson, forceFirstPerson, 50790);
    TiltedPhoques::ThisCall(forceFirstPerson, this);
}

void PlayerCamera::ForceThirdPerson() noexcept
{
    TP_THIS_FUNCTION(TForceThirdPerson, void, PlayerCamera);
    POINTER_SKYRIMSE(TForceThirdPerson, forceThirdPerson, 50796);
    TiltedPhoques::ThisCall(forceThirdPerson, this);
}
