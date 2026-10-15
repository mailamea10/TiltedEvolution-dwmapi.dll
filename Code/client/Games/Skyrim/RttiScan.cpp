#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/RttiScan.h>

#include <algorithm>
#include <cstring>

namespace RttiScan
{
// Finds a class's primary vtable from its MSVC RTTI type name, scanning the
// game image's sections.
void** FindVTable(const char* acpMangledName) noexcept
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

} // namespace RttiScan
