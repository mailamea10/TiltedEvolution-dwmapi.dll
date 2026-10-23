#include <TiltedOnlinePCH.h>

#include <Components/TESSpellList.h>
#include <Games/Memory.h>

void TESSpellList::Initialize()
{
    if (lists != nullptr)
        return;

    lists = Memory::New<Lists>();
}

namespace
{
template <class T> uint32_t CompactNonNull(T** apEntries, uint32_t& aCount) noexcept
{
    if (!apEntries)
        return 0;

    uint32_t kept = 0;
    for (uint32_t i = 0; i < aCount; ++i)
    {
        if (apEntries[i])
            apEntries[kept++] = apEntries[i];
    }

    const uint32_t cRemoved = aCount - kept;
    aCount = kept;
    return cRemoved;
}
} // namespace

uint32_t TESSpellList::RemoveNullEntries() noexcept
{
    if (!lists)
        return 0;

    return CompactNonNull(lists->spells, lists->spellCount) + CompactNonNull(lists->unk4, lists->unk4Count) +
           CompactNonNull(lists->shouts, lists->shoultCount);
}
