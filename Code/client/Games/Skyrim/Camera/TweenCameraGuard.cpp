#include <TiltedOnlinePCH.h>

#include <Camera/PlayerCamera.h>
#include <Camera/TESCamera.h>
#include <World.h>

#include <cstring>

// Keeps the Tween menu camera in place on Skyrim SE 1.5.x while connected.
//
// TweenMenuCameraState only ever rotates the camera root; it never writes a
// position. In single player that does not matter because the Tween menu pauses
// the game and renders a frozen frame. Skyrim Together unpauses it, so
// PlayerCamera keeps updating every frame. Camera mods that add an offset to the
// camera position on top of whatever the active state produced (Camera Noise on
// Nolvus, for one) then stack that offset frame after frame, because nothing
// resets the position while this state is active. The camera drifts off through
// walls and roofs within a few seconds.
//
// The guard captures the camera root and camera node positions when the state
// begins, and puts them back before each update of the state. Mods still apply
// their per-frame offset afterwards, but it stays bounded.
//
// The vtable is found through the game's own RTTI, by type name, so no address
// library id is needed.

#ifdef SKYRIM_TARGET_LEGACY

namespace
{
// NiAVObject::local.translate in SE/AE layout (local transform at 0x48: a 3x3
// rotation, then the translation).
constexpr size_t kLocalTranslateOffset = 0x6C;

// TESCameraState vtable slots: 0 dtor, 1 Begin, 2 End, 3 Update.
constexpr size_t kBeginSlot = 1;
constexpr size_t kEndSlot = 2;
constexpr size_t kUpdateSlot = 3;

using TBegin = void(void*);
using TEnd = void(void*);
using TUpdate = void(void*, void*);

TBegin* RealBegin = nullptr;
TEnd* RealEnd = nullptr;
TUpdate* RealUpdate = nullptr;

struct SavedPositions
{
    bool Valid = false;
    void* pRoot = nullptr;
    float Root[3]{};
    void* pCamera = nullptr;
    float Camera[3]{};
};

SavedPositions s_saved{};

float* LocalTranslate(void* apObject)
{
    return reinterpret_cast<float*>(static_cast<uint8_t*>(apObject) + kLocalTranslateOffset);
}

bool IsConnected()
{
    return World::Get().GetTransport().IsConnected();
}

void Capture()
{
    s_saved = {};

    auto* pCamera = static_cast<TESCamera*>(PlayerCamera::Get());
    if (!pCamera || !pCamera->cameraNode)
        return;

    s_saved.pRoot = pCamera->cameraNode;
    std::memcpy(s_saved.Root, LocalTranslate(s_saved.pRoot), sizeof(s_saved.Root));

    if (auto* pNiCamera = pCamera->GetNiCamera())
    {
        s_saved.pCamera = pNiCamera;
        std::memcpy(s_saved.Camera, LocalTranslate(s_saved.pCamera), sizeof(s_saved.Camera));
    }

    s_saved.Valid = true;
}

void Restore()
{
    auto* pCamera = static_cast<TESCamera*>(PlayerCamera::Get());
    // The nodes can be swapped (cell change, 3D reload); only write to the ones captured.
    if (!pCamera || pCamera->cameraNode != s_saved.pRoot)
    {
        s_saved.Valid = false;
        return;
    }

    std::memcpy(LocalTranslate(s_saved.pRoot), s_saved.Root, sizeof(s_saved.Root));
    if (s_saved.pCamera && pCamera->GetNiCamera() == s_saved.pCamera)
        std::memcpy(LocalTranslate(s_saved.pCamera), s_saved.Camera, sizeof(s_saved.Camera));
}

void HookBegin(void* apThis)
{
    RealBegin(apThis);
    Capture();
}

void HookEnd(void* apThis)
{
    s_saved.Valid = false;
    RealEnd(apThis);
}

void HookUpdate(void* apThis, void* apNextState)
{
    if (IsConnected())
    {
        if (!s_saved.Valid)
            Capture();
        else
            Restore();
    }

    // The original rotates the root and refreshes its world transform, so the
    // restored position is what gets propagated this frame.
    RealUpdate(apThis, apNextState);
}

// Finds a class's primary vtable from its MSVC RTTI type name, scanning the
// game image's sections.
void** FindVTableByTypeName(const char* acpMangledName)
{
    auto* pBase = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS64*>(pBase + pDos->e_lfanew);
    auto* pSection = IMAGE_FIRST_SECTION(pNt);

    const size_t nameLength = std::strlen(acpMangledName);

    auto forEachData = [&](auto&& aCallback) -> uint8_t*
    {
        for (int i = 0; i < pNt->FileHeader.NumberOfSections; ++i)
        {
            const auto& section = pSection[i];
            if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)
                continue;

            uint8_t* pStart = pBase + section.VirtualAddress;
            const size_t size = std::max<size_t>(section.Misc.VirtualSize, 0) & ~size_t(3);
            if (uint8_t* pFound = aCallback(pStart, size))
                return pFound;
        }
        return nullptr;
    };

    // TypeDescriptor: pVFTable, spare, then the mangled name.
    uint8_t* pName = forEachData(
        [&](uint8_t* apStart, size_t aSize) -> uint8_t*
        {
            for (size_t i = 0; i + nameLength + 1 <= aSize; ++i)
                if (apStart[i] == '.' && std::memcmp(apStart + i, acpMangledName, nameLength + 1) == 0)
                    return apStart + i;
            return nullptr;
        });
    if (!pName)
        return nullptr;

    const uint32_t typeDescriptorRva = static_cast<uint32_t>(pName - 16 - pBase);

    // CompleteObjectLocator: signature 1, offset 0, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf (all RVAs).
    uint8_t* pLocator = forEachData(
        [&](uint8_t* apStart, size_t aSize) -> uint8_t*
        {
            for (size_t i = 0; i + 24 <= aSize; i += 4)
            {
                const auto* p = reinterpret_cast<const uint32_t*>(apStart + i);
                if (p[0] == 1 && p[1] == 0 && p[3] == typeDescriptorRva && p[5] == static_cast<uint32_t>(apStart + i - pBase))
                    return apStart + i;
            }
            return nullptr;
        });
    if (!pLocator)
        return nullptr;

    const uint64_t locatorAddress = reinterpret_cast<uint64_t>(pLocator);
    uint8_t* pMeta = forEachData(
        [&](uint8_t* apStart, size_t aSize) -> uint8_t*
        {
            for (size_t i = 0; i + 8 <= aSize; i += 8)
                if (*reinterpret_cast<const uint64_t*>(apStart + i) == locatorAddress)
                    return apStart + i;
            return nullptr;
        });
    if (!pMeta)
        return nullptr;

    return reinterpret_cast<void**>(pMeta + 8);
}

void* SwapSlot(void** apVTable, size_t aSlot, void* apHook)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(&apVTable[aSlot], sizeof(void*), PAGE_READWRITE, &oldProtect))
        return nullptr;

    void* pOriginal = apVTable[aSlot];
    apVTable[aSlot] = apHook;
    VirtualProtect(&apVTable[aSlot], sizeof(void*), oldProtect, &oldProtect);
    return pOriginal;
}
} // namespace

// Called from RunTiltedInit; an explicit call keeps this translation unit from
// being dropped by the linker (a self-registering static initializer was not
// enough for the runtime DLL).
void InstallTweenCameraGuard() noexcept
{
    {
        void** pVTable = FindVTableByTypeName(".?AVTweenMenuCameraState@@");
        if (!pVTable)
        {
            spdlog::warn("tween camera guard: TweenMenuCameraState vtable not found, the Tween menu camera is left as is");
            return;
        }

        RealBegin = static_cast<TBegin*>(SwapSlot(pVTable, kBeginSlot, reinterpret_cast<void*>(&HookBegin)));
        RealEnd = static_cast<TEnd*>(SwapSlot(pVTable, kEndSlot, reinterpret_cast<void*>(&HookEnd)));
        RealUpdate = static_cast<TUpdate*>(SwapSlot(pVTable, kUpdateSlot, reinterpret_cast<void*>(&HookUpdate)));

        if (!RealBegin || !RealEnd || !RealUpdate)
        {
            spdlog::error("tween camera guard: could not patch the TweenMenuCameraState vtable");
            return;
        }

        spdlog::info("tween camera guard: installed on TweenMenuCameraState vtable at {}", fmt::ptr(pVTable));
    }
}

#endif
