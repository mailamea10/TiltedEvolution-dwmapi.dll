#pragma once

#include <Components/BaseFormComponent.h>

struct SpellItem;
struct TESForm;
struct TESShout;

struct TESSpellList : BaseFormComponent
{
    struct Lists
    {
        SpellItem** spells{nullptr};
        TESForm** unk4{nullptr};
        TESShout** shouts{nullptr};
        uint32_t spellCount{0};
        uint32_t unk4Count{0};
        uint32_t shoultCount{0};
    };

    Lists* lists;

    void Initialize();
    // Drops null spells/shouts left by a change-form load, returns how many were dropped
    uint32_t RemoveNullEntries() noexcept;
};

static_assert(sizeof(TESSpellList::Lists) == 0x28);
