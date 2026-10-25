#include "nv_journal.h"
#include <flash.h>
#include <generated/memory_map.h>
#include <stddef.h>
#include <string.h>

#define RECORD_MAGIC 0x4E564A31U
#define RECORD_COMMIT 0x434F4D54U
#define PAGE_BYTES 1024U

typedef struct NvRecord
{
    uint32_t magic;
    uint32_t sequence;
    NvState state;
    uint32_t crc;
    uint32_t commit;
} NvRecord;

_Static_assert(sizeof(NvState) == 68, "NV format changed: version the journal");
_Static_assert(sizeof(NvRecord) == 84, "NV records must be word aligned");
_Static_assert(NVITEMS_SIZE == 4 * PAGE_BYTES, "journal and backups require four pages");

uint32_t nvChecksum(const void* data, uint32_t size)
{
    const uint8_t* bytes = data;
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool erased(uint32_t address, uint32_t size)
{
    const volatile uint32_t* words = (const volatile uint32_t*)address;
    for (uint32_t i = 0; i < size / 4; ++i)
        if (words[i] != UINT32_MAX)
            return false;
    return true;
}

static bool validState(const NvState* state)
{
    if (state->items.active_app > BOOT_MODE || state->items.app_valid > 1 ||
        state->items.boot_count > MAX_FAILED_BOOT_COUNTS || state->reserved != 0 ||
        state->phase > SWAP_FW1_WRITTEN)
        return false;
    if (state->phase == SWAP_IDLE)
        return true;
    return state->page <= FW_SIZE / PAGE_BYTES &&
           (state->page < FW_SIZE / PAGE_BYTES || state->phase == SWAP_READY) &&
           (state->direction == SWAP_FORWARD || state->direction == SWAP_ROLLBACK) &&
           state->next_active >= FW1 && state->next_active <= BOOT_MODE &&
           state->next_active % 2 == FW1 && state->old_valid <= 1;
}

static bool validRecord(const NvRecord* record)
{
    return record->magic == RECORD_MAGIC && record->commit == RECORD_COMMIT &&
           record->crc == nvChecksum(record, offsetof(NvRecord, crc)) &&
           validState(&record->state);
}

static const NvRecord* latestRecord(void)
{
    const NvRecord* latest = NULL;
    for (uint32_t page = NV_JOURNAL_A_ADDRESS; page <= NV_JOURNAL_B_ADDRESS; page += PAGE_BYTES)
        for (uint32_t offset = 0; offset + sizeof(NvRecord) <= PAGE_BYTES; offset += sizeof(NvRecord))
        {
            const NvRecord* record = (const NvRecord*)(page + offset);
            /* Serial-number arithmetic also handles sequence wraparound. */
            if (validRecord(record) && (latest == NULL ||
                (record->sequence != latest->sequence &&
                 record->sequence - latest->sequence < 0x80000000U)))
                latest = record;
        }
    return latest;
}

bool nvStateLoad(NvState* state)
{
    memset(state, 0, sizeof(*state));
    const NvRecord* latest = latestRecord();
    if (latest != NULL)
    {
        *state = latest->state;
        return true;
    }

    /* First installation: preserve legacy NV in A while the initial record is
     * committed in B. An interrupted initial B write can safely be retried.
     * Never turn a damaged committed journal into a new/empty transaction. */
    for (uint32_t offset = 0; offset + sizeof(NvRecord) <= PAGE_BYTES; offset += sizeof(NvRecord))
        if (((const NvRecord*)(NV_JOURNAL_B_ADDRESS + offset))->commit == RECORD_COMMIT)
            return false;
    if (erased(NV_JOURNAL_A_ADDRESS, PAGE_BYTES))
        return true;
    if (erased(NV_JOURNAL_A_ADDRESS + 8, PAGE_BYTES - 8))
    {
        memcpy(&state->items, (const void*)NV_JOURNAL_A_ADDRESS, sizeof(state->items));
        if (validState(state))
            return true;
    }
    memset(state, 0, sizeof(*state));
    state->items.active_app = BOOT_MODE;
    return false;
}

bool nvStateStore(const NvState* state)
{
    if (!validState(state) || getPageSize() != PAGE_BYTES)
        return false;
    NvState previous;
    if (!nvStateLoad(&previous))
        return false;
    const NvRecord* latest = latestRecord();
    uint32_t page = latest == NULL ? NV_JOURNAL_B_ADDRESS :
                    (uint32_t)(uintptr_t)latest / PAGE_BYTES * PAGE_BYTES;
    uint32_t sequence = latest == NULL ? 0 : latest->sequence + 1;
    uint32_t address = 0;
    /* Skip torn records; never program over a partially written halfword. */
    for (uint32_t offset = latest == NULL ? 0 :
         (uint32_t)(uintptr_t)latest - page + sizeof(NvRecord);
         offset + sizeof(NvRecord) <= PAGE_BYTES; offset += sizeof(NvRecord))
        if (erased(page + offset, sizeof(NvRecord)))
        {
            address = page + offset;
            break;
        }
    if (address == 0)
    {
        /* Keep the page containing the latest committed record untouched. */
        if (latest != NULL)
            page = page == NV_JOURNAL_A_ADDRESS ? NV_JOURNAL_B_ADDRESS : NV_JOURNAL_A_ADDRESS;
        if (!flashErasePage(page))
            return false;
        address = page;
    }
    NvRecord record = {.magic = RECORD_MAGIC, .sequence = sequence, .state = *state};
    record.crc = nvChecksum(&record, offsetof(NvRecord, crc));
    record.commit = RECORD_COMMIT;
    if (!flashProgram(address, (const uint32_t*)&record, offsetof(NvRecord, commit) / 4))
        return false;
    /* STM32F1 words consist of two halfword writes. A torn commit is invalid;
     * the old record remains authoritative until this exact marker is present. */
    if (!flashProgram(address + offsetof(NvRecord, commit), &record.commit, 1))
        return false;
    return validRecord((const NvRecord*)address);
}

bool nvUpdateAllowed(void)
{
    NvState state;
    return nvStateLoad(&state) && state.phase == SWAP_IDLE;
}
