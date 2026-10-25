#include "bt_interface.h"
#include <firmware_details.h>
#include <flash.h>
#include <generated/memory_map.h>
#include <nv_journal.h>
#include <string.h>

#define PAGE_BYTES 1024U
#define PAGE_COUNT (FW_SIZE / PAGE_BYTES)

static uint32_t pageChecksum(uint32_t address)
{
    return nvChecksum((const void*)address, PAGE_BYTES);
}

static bool copyPage(uint32_t source, uint32_t destination, uint32_t expected)
{
    /* Validate before erasing anything. RAM is never a recovery source. */
    return pageChecksum(source) == expected &&
           flashEraseAndProgram(destination, (const uint32_t*)source, PAGE_BYTES / 4) &&
           pageChecksum(destination) == expected;
}

static bool imageMatches(uint32_t bank, const uint8_t* digest)
{
    return firmwareImageCheck(bank) &&
           memcmp(((const FirmwareDetails*)bank)->md5_sign, digest, MD5_SIZE) == 0;
}

bool firmwareSwapStart(const NonVolatileItems* selection)
{
    NvState state;
    if (!nvStateLoad(&state) || state.phase != SWAP_IDLE ||
        !isFirmware2Active(selection) || selection->active_app >= BOOT_MODE ||
        getPageSize() != PAGE_BYTES)
        return false;
    /* Keep a second gate at the swap entry itself: no caller may create a
     * transaction or erase backups/bank pages for an invalid candidate. */
    if (!firmwareImageCheck(FW2_BANK_ADDRESS))
    {
        bootLog("BL FW2 REJECTED INTEGRITY\r\n");
        return false;
    }
    memset(&state, 0, sizeof(state));
    state.items = *selection;
    state.items.boot_count = 0; /* Resets during copying are not failed application boots. */
    state.phase = SWAP_READY;
    state.direction = SWAP_FORWARD;
    state.next_active = selection->active_app + 1;
    state.old_valid = firmwareImageCheck(FW1_BANK_ADDRESS);
    memcpy(state.old_digest, ((const FirmwareDetails*)FW1_BANK_ADDRESS)->md5_sign, MD5_SIZE);
    memcpy(state.new_digest, ((const FirmwareDetails*)FW2_BANK_ADDRESS)->md5_sign, MD5_SIZE);
    /* No backup or firmware erase before transaction intent is committed. */
    return nvStateStore(&state) && firmwareSwapResume();
}

bool firmwareSwapResume(void)
{
    NvState state;
    if (!nvStateLoad(&state) || getPageSize() != PAGE_BYTES)
        return false;
    while (state.phase != SWAP_IDLE)
    {
        if (state.page == PAGE_COUNT)
        {
            if (!imageMatches(FW1_BANK_ADDRESS, state.new_digest))
            {
                /* Reverse a rejected image using the same four phases. */
                if (state.direction != SWAP_FORWARD || !state.old_valid ||
                    !imageMatches(FW2_BANK_ADDRESS, state.old_digest))
                    return false;
                memcpy(state.new_digest, state.old_digest, MD5_SIZE);
                state.old_valid = 0;
                state.direction = SWAP_ROLLBACK;
                state.page = 0;
                if (!nvStateStore(&state))
                    return false;
                bootLog("BL SWAP ROLLBACK\r\n");
                continue;
            }
            if (state.old_valid && !imageMatches(FW2_BANK_ADDRESS, state.old_digest))
                return false;
            /* Completion and active selection are ONE journal commit. */
            const uint8_t active = state.next_active;
            NonVolatileItems items = state.items;
            memset(&state, 0, sizeof(state));
            state.items = items;
            setActiveApp(&state.items, active);
            if (!nvStateStore(&state))
                return false;
            bootLog("BL SWAP DONE\r\n");
            return true;
        }

        uint32_t fw1 = FW1_BANK_ADDRESS + state.page * PAGE_BYTES;
        uint32_t fw2 = FW2_BANK_ADDRESS + state.page * PAGE_BYTES;
        switch (state.phase)
        {
        case SWAP_READY:
            state.old_page_crc = pageChecksum(fw1);
            if (!copyPage(fw1, NV_OLD_PAGE_ADDRESS, state.old_page_crc))
                return false;
            state.phase = SWAP_OLD_SAVED;
            break;
        case SWAP_OLD_SAVED:
            if (pageChecksum(NV_OLD_PAGE_ADDRESS) != state.old_page_crc)
                return false;
            state.new_page_crc = pageChecksum(fw2);
            if (!copyPage(fw2, NV_NEW_PAGE_ADDRESS, state.new_page_crc))
                return false;
            state.phase = SWAP_BOTH_SAVED;
            break;
        case SWAP_BOTH_SAVED:
            if (pageChecksum(NV_OLD_PAGE_ADDRESS) != state.old_page_crc ||
                !copyPage(NV_NEW_PAGE_ADDRESS, fw1, state.new_page_crc))
                return false;
            state.phase = SWAP_FW1_WRITTEN;
            break;
        case SWAP_FW1_WRITTEN:
            /* Retain both backups until the bank writes have been verified. */
            if (pageChecksum(fw1) != state.new_page_crc &&
                !copyPage(NV_NEW_PAGE_ADDRESS, fw1, state.new_page_crc))
                return false;
            if (!copyPage(NV_OLD_PAGE_ADDRESS, fw2, state.old_page_crc))
                return false;
            ++state.page;
            state.phase = SWAP_READY;
            break;
        default:
            return false;
        }
        /* Repeating a copy after a torn status record is safe. Never reuse
         * backups or start a dependent copy before this phase is durable. */
        if (!nvStateStore(&state))
            return false;
    }
    return true;
}
