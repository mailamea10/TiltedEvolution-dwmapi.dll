// Keeps the low 2 GB of the address space available for code that needs it, on
// the 1.5.x launcher.
//
// Plugins built on LuaJIT 2.x without GC64 (JContainers 4.1.x for Skyrim SE, for
// one) can only allocate their Lua heap below 2 GB. They ask ntdll for it with
// NtAllocateVirtualMemory and a non-zero ZeroBits. SkyrimSE.exe is a high-entropy
// ASLR process, so the heap normally lives far above 4 GB and the low range stays
// free. The immersive launcher has to sit at the game's fixed base, so its
// process allocates bottom-up from the start of the address space. By the time a
// save is loaded the low 2 GB are full of ordinary heap memory, luaL_newstate()
// returns null and JContainers crashes on the next Lua call.
//
// At startup the launcher reserves (not commits) the free low range in fixed-size
// chunks, so ordinary allocations land above 2 GB as they would in the game's own
// process. NtAllocateVirtualMemory is hooked: when a request that needs low memory
// (ZeroBits set, or an explicit address below 2 GB) cannot be satisfied, chunks are
// handed back and the request is retried.

#ifdef SKYRIM_TARGET_LEGACY

#include <Windows.h>
#include <winternl.h>

#include <MinHook.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace
{
constexpr uintptr_t kLowStart = 0x10000000;  // leave the first 256 MB to the loader and early system allocations
constexpr uintptr_t kLowLimit = 0x80000000;  // 2 GB
constexpr size_t kChunkSize = 16 * 1024 * 1024;

using TNtAllocateVirtualMemory = NTSTATUS(NTAPI*)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
TNtAllocateVirtualMemory RealNtAllocateVirtualMemory = nullptr;

std::mutex s_lock;
std::vector<uintptr_t> s_chunks; // sorted ascending; released from the top down

bool IsCurrentProcess(HANDLE aProcess)
{
    return aProcess == GetCurrentProcess() || GetProcessId(aProcess) == GetCurrentProcessId();
}

// Releases the reserved chunk covering aAddress, or the highest one if aAddress is 0.
bool ReleaseChunk(uintptr_t aAddress)
{
    std::scoped_lock lock(s_lock);
    if (s_chunks.empty())
        return false;

    auto it = s_chunks.end() - 1;
    if (aAddress)
    {
        it = std::upper_bound(s_chunks.begin(), s_chunks.end(), aAddress);
        if (it == s_chunks.begin())
            return false;
        --it;
        if (aAddress >= *it + kChunkSize)
            return false;
    }

    VirtualFree(reinterpret_cast<void*>(*it), 0, MEM_RELEASE);
    s_chunks.erase(it);
    return true;
}

NTSTATUS NTAPI HookNtAllocateVirtualMemory(HANDLE aProcess, PVOID* apBase, ULONG_PTR aZeroBits, PSIZE_T apSize, ULONG aType, ULONG aProtect)
{
    const PVOID requestedBase = apBase ? *apBase : nullptr;
    const SIZE_T requestedSize = apSize ? *apSize : 0;

    NTSTATUS status = RealNtAllocateVirtualMemory(aProcess, apBase, aZeroBits, apSize, aType, aProtect);
    if (status >= 0 || !apBase || !apSize || !IsCurrentProcess(aProcess))
        return status;

    const auto requested = reinterpret_cast<uintptr_t>(requestedBase);
    const bool wantsLowRange = aZeroBits != 0 || (requested != 0 && requested < kLowLimit);
    if (!wantsLowRange)
        return status;

    // Hand back the chunk under an explicit address, otherwise any chunk, and retry.
    while (status < 0)
    {
        bool released = false;
        if (requested)
        {
            for (uintptr_t p = requested & ~(uintptr_t(kChunkSize) - 1); p < requested + requestedSize; p += kChunkSize)
                released |= ReleaseChunk(p);
        }
        else
            released = ReleaseChunk(0);

        if (!released)
            break;

        *apBase = requestedBase;
        *apSize = requestedSize;
        status = RealNtAllocateVirtualMemory(aProcess, apBase, aZeroBits, apSize, aType, aProtect);
    }

    return status;
}

void ReserveLowRange()
{
    // Runs before the hook is enabled; the list is published at the end so no lock
    // is held across VirtualAlloc.
    std::vector<uintptr_t> chunks;

    MEMORY_BASIC_INFORMATION info{};
    for (uintptr_t address = kLowStart; address < kLowLimit; address += kChunkSize)
    {
        if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_FREE)
            continue;

        const auto regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd < address + kChunkSize)
            continue;

        if (VirtualAlloc(reinterpret_cast<void*>(address), kChunkSize, MEM_RESERVE, PAGE_NOACCESS))
            chunks.push_back(address);
    }

    std::scoped_lock lock(s_lock);
    s_chunks = std::move(chunks);
}
} // namespace

// Must run after MH_Initialize and before MH_EnableHook(nullptr), as early as possible.
void InstallLowMemoryReserve()
{
    MH_CreateHookApi(L"ntdll.dll", "NtAllocateVirtualMemory", reinterpret_cast<void*>(&HookNtAllocateVirtualMemory),
                     reinterpret_cast<void**>(&RealNtAllocateVirtualMemory));
    ReserveLowRange();
}

#endif
