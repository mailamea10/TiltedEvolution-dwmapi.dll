#pragma once

#include <Structs/Inventory.h>
#include <ExtraData/ExtraDataList.h>

/**
 * @brief Dispatched when the contents of an object or actor inventory changes locally.
 */
struct InventoryChangeEvent
{
    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem)
        : FormId(aFormId)
        , Item(std::move(arItem))
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aUpdateClients)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , UpdateClients(aUpdateClients)
    {
    }

    uint32_t FormId{};
    Inventory::Entry Item{};
    bool UpdateClients = true;
    // The item moved to or from the local player, e.g. looting or pickpocketing an actor owned by someone else
    bool IsPlayerTransfer = false;
};
