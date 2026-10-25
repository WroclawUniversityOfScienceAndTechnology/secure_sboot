#include "non_volatile_items.h"

#include "nv_journal.h"
#include <generated/memory_map.h>
#include <string.h>


bool isMaxBootCount(const NonVolatileItems* nv_items)
{
    return nv_items->boot_count >= MAX_FAILED_BOOT_COUNTS;
}

bool isAppValid(const NonVolatileItems* nv_items)
{
    return nv_items->app_valid == 1;
}

bool isActiveFirmwareSelected(const NonVolatileItems* nv_items)
{
    return nv_items->active_app != FW_NONE;
}

void increaseActiveApp(NonVolatileItems* nv_items)
{
    ++nv_items->active_app;
    nv_items->boot_count = 0;
    nv_items->app_valid = 0;
}

void increaseBootCount(NonVolatileItems* nv_items)
{
    ++nv_items->boot_count;
}

bool isFirmware1Active(const NonVolatileItems* nv_items)
{
    return nv_items->active_app % 2 == FW1;
}

bool isFirmware2Active(const NonVolatileItems* nv_items)
{
    return (nv_items->active_app != FW_NONE) && (((nv_items->active_app % 2) + FW2) == FW2);
}

void setActiveApp(NonVolatileItems* nv_items, uint8_t active_app)
{
    nv_items->active_app = active_app;
    nv_items->boot_count = 0;
    nv_items->app_valid = 0;
}

bool isBootModeActive(const NonVolatileItems* nv_items)
{
    return nv_items->active_app == BOOT_MODE;
}

bool nviStore(uint32_t nvi_address, const NonVolatileItems* nvi)
{
    NvState state;
    if (nvi_address != NVITEMS_ADDRESS || !nvStateLoad(&state) || state.phase != SWAP_IDLE)
        return false;
    state.items = *nvi;
    return nvStateStore(&state);
}

bool nviLoad(uint32_t nvi_address, NonVolatileItems* nvi)
{
    NvState state;
    if (nvi_address != NVITEMS_ADDRESS || !nvStateLoad(&state))
    {
        memset(nvi, 0, sizeof(*nvi));
        nvi->active_app = BOOT_MODE;
        return false;
    }
    *nvi = state.items;
    return true;
}

bool nviUpdate(uint32_t nvi_address, const NonVolatileItems* nvi)
{
    NvState state;
    if (nvi_address != NVITEMS_ADDRESS || !nvStateLoad(&state) || state.phase != SWAP_IDLE)
        return false;
    if (memcmp(&state.items, nvi, sizeof(*nvi)) == 0)
        return true;
    state.items = *nvi;
    return nvStateStore(&state);
}
