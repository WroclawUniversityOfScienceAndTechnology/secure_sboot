#define _GNU_SOURCE
#include <assert.h>
#include <bt_interface.h>
#include <firmware_details.h>
#include <flash.h>
#include <generated/memory_map.h>
#include <nv_journal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "commands/cmd_dfu.h"
#include "flash_mock.h"

#define FLASH_BYTES 65536U
#define PAGE_BYTES 1024U
#define FLASH ((uint8_t*)0x08000000)

typedef struct Checkpoint
{
    uint8_t flash[FLASH_BYTES];
    uint8_t data[PAGE_BYTES];
    enum FlashOperation operation;
    uint32_t address;
    uint32_t words;
} Checkpoint;

static Checkpoint* checkpoints;
static unsigned checkpoint_count;
static uint8_t initial_flash[FLASH_BYTES];
static uint8_t pending_final[FLASH_BYTES];
static uint8_t expected_old[FW_SIZE], expected_new[FW_SIZE];
static unsigned reset_cases;

static void capture(enum FlashOperation operation, uint32_t address, const uint32_t* data, uint32_t words)
{
    checkpoints = realloc(checkpoints, (checkpoint_count + 1) * sizeof(*checkpoints));
    assert(checkpoints);
    Checkpoint* point = &checkpoints[checkpoint_count++];
    memcpy(point->flash, FLASH, FLASH_BYTES);
    point->operation = operation;
    point->address = address;
    point->words = words;
    if (data)
        memcpy(point->data, data, words * 4);
    NvState state;
    assert(nvStateLoad(&state));
    if (state.phase == SWAP_READY && state.page == FW_SIZE / PAGE_BYTES &&
        state.direction == SWAP_FORWARD)
        memcpy(pending_final, FLASH, FLASH_BYTES);
}

static void checkBoot(const uint8_t* fw1, const uint8_t* fw2)
{
    NonVolatileItems items;
    if (!prepareFirmwareBoot(&items))
    {
        NvState state;
        bool loaded = nvStateLoad(&state);
        fprintf(stderr, "recovery failed in reset case %u: loaded=%d phase=%u page=%u\n",
                reset_cases, loaded, state.phase, state.page);
        abort();
    }
    assert(isFirmware1Active(&items) && !isBootModeActive(&items));
    assert(firmwareImageCheck(FW1_BANK_ADDRESS));
    assert(memcmp((const void*)FW1_BANK_ADDRESS, fw1, FW_SIZE) == 0);
    if (fw2)
    {
        assert(firmwareImageCheck(FW2_BANK_ADDRESS));
        assert(memcmp((const void*)FW2_BANK_ADDRESS, fw2, FW_SIZE) == 0);
    }
    NvState state;
    assert(nvStateLoad(&state) && state.phase == SWAP_IDLE);
    assert(items.boot_count >= 1 && items.boot_count <= 2);
    assert(memcmp(FLASH, initial_flash, BOOTLOADER_SIZE) == 0);
}

static void cut(const Checkpoint* point, uint32_t bytes, bool torn)
{
    memcpy(FLASH, point->flash, FLASH_BYTES);
    uint8_t* destination = (uint8_t*)(uintptr_t)point->address;
    if (point->operation == ERASE_PAGE)
    {
        memset(destination, 0xff, bytes);
        /* Model an incompletely erased halfword, retaining some zero bits. */
        if (torn && bytes < PAGE_BYTES)
            destination[bytes] |= 0x55;
    }
    else
    {
        memcpy(destination, point->data, bytes);
        if (torn && bytes < point->words * 4)
            destination[bytes] &= point->data[bytes] | 0x55;
    }
    ++reset_cases;
}

static void testCuts(const char* name, const uint8_t* fw1, const uint8_t* fw2)
{
    const unsigned first = reset_cases;
    for (unsigned i = 0; i < checkpoint_count; ++i)
    {
        const Checkpoint* point = &checkpoints[i];
        if (point->operation == ERASE_PAGE)
        {
            const uint32_t sizes[] = {0, 2, 510, 512, 1022, 1024};
            for (unsigned j = 0; j < sizeof(sizes) / sizeof(sizes[0]); ++j)
            {
                cut(point, sizes[j], true);
                checkBoot(fw1, fw2);
            }
        }
        else
        {
            /* Every STM32F1 halfword boundary, including the two halves of
             * each commit marker and a partially programmed next halfword. */
            for (uint32_t bytes = 0; bytes <= point->words * 4; bytes += 2)
            {
                cut(point, bytes, true);
                checkBoot(fw1, fw2);
            }
        }
    }
    printf("PASS: %s, %u reset positions across %u flash operations\n",
           name, reset_cases - first, checkpoint_count);
    fflush(stdout);
}

static void testNvWrites(bool legacy)
{
    memset(FLASH + BOOTLOADER_SIZE, 0xff, FLASH_BYTES - BOOTLOADER_SIZE);
    NonVolatileItems before = {0};
    if (legacy)
    {
        before.active_app = FW1;
        before.app_valid = 1;
        memcpy((void*)NVITEMS_ADDRESS, &before, sizeof(before));
    }
    NonVolatileItems after = before;
    setActiveApp(&after, FW2);
    checkpoint_count = 0;
    before_flash = capture;
    assert(nviStore(NVITEMS_ADDRESS, &after));
    before_flash = NULL;
    for (unsigned i = 0; i < checkpoint_count; ++i)
        for (uint32_t bytes = 0; bytes <= checkpoints[i].words * 4; bytes += 2)
        {
            cut(&checkpoints[i], bytes, true);
            NonVolatileItems actual;
            assert(nviLoad(NVITEMS_ADDRESS, &actual));
            assert(memcmp(&actual, &before, sizeof(actual)) == 0 ||
                   memcmp(&actual, &after, sizeof(actual)) == 0);
            assert(nviStore(NVITEMS_ADDRESS, &after));
            assert(nviLoad(NVITEMS_ADDRESS, &actual));
            assert(memcmp(&actual, &after, sizeof(actual)) == 0);
        }
    puts(legacy ? "PASS: interrupted legacy NV migration" : "PASS: interrupted first NV initialization");
}

static void loadImage(const char* path, uint32_t bank)
{
    FILE* file = fopen(path, "rb");
    assert(file);
    assert(fread((void*)(uintptr_t)bank, 1, FW_SIZE, file) == FW_SIZE);
    fclose(file);
    assert(firmwareImageCheck(bank));
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    assert(mmap(FLASH, FLASH_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == FLASH);
    memset(FLASH, 0x5a, BOOTLOADER_SIZE);
    testNvWrites(false);
    testNvWrites(true);
    memset(FLASH + BOOTLOADER_SIZE, 0xff, FLASH_BYTES - BOOTLOADER_SIZE);
    loadImage(argv[1], FW1_BANK_ADDRESS);
    loadImage(argv[2], FW2_BANK_ADDRESS);
    memcpy(expected_old, (const void*)FW1_BANK_ADDRESS, FW_SIZE);
    memcpy(expected_new, (const void*)FW2_BANK_ADDRESS, FW_SIZE);
    NonVolatileItems items = {.active_app = FW2};
    assert(nviStore(NVITEMS_ADDRESS, &items));
    memcpy(initial_flash, FLASH, FLASH_BYTES);

    checkpoint_count = 0;
    before_flash = capture;
    assert(prepareFirmwareBoot(&items));
    before_flash = NULL;
    testCuts("forward swap / journal rollover / completion", expected_new, expected_old);

    /* A reported flash error must stop immediately and leave a resumable state. */
    for (unsigned failure = 1; failure <= checkpoint_count; ++failure)
    {
        memcpy(FLASH, initial_flash, FLASH_BYTES);
        writes = 0;
        fail_write = failure;
        assert(!prepareFirmwareBoot(&items));
        assert(writes == failure);
        fail_write = 0;
        checkBoot(expected_new, expected_old);
    }
    puts("PASS: recovery after every reported flash-operation failure");

    /* Damage a backup while banks are partial: fail closed, preserve banks,
     * and reject DFU/RAA/confirmation writes that could destroy recovery data. */
    for (unsigned i = 0; i < checkpoint_count; ++i)
    {
        memcpy(FLASH, checkpoints[i].flash, FLASH_BYTES);
        NvState state;
        assert(nvStateLoad(&state));
        if (state.phase != SWAP_BOTH_SAVED)
            continue;
        assert(!nvUpdateAllowed());
        assert(!nviStore(NVITEMS_ADDRESS, &items));
        assert(!nviUpdate(NVITEMS_ADDRESS, &items));
        AtMessage request = {.data = "=2", .data_size = 2};
        uint8_t response[32];
        uint16_t size;
        cmdDfuOpen(&request, response, &size);
        assert(strcmp((const char*)response, "ERROR\n") == 0);
        ((uint8_t*)NV_OLD_PAGE_ADDRESS)[10] ^= 1;
        unsigned previous_writes = writes;
        assert(!prepareFirmwareBoot(&items));
        assert(previous_writes == writes);
        break;
    }
    puts("PASS: damaged backup detected, pending transaction protected from commands");

    /* Repeated cuts during one transaction must not consume application trials. */
    memcpy(FLASH, initial_flash, FLASH_BYTES);
    for (unsigned reboot = 0; reboot < 100; ++reboot)
    {
        writes = 0;
        fail_write = reboot < 40 ? 5 : 3;
        assert(!prepareFirmwareBoot(&items));
        NvState state;
        assert(nvStateLoad(&state));
        assert(state.phase != SWAP_IDLE && state.items.boot_count == 0);
    }
    fail_write = 0;
    checkBoot(expected_new, expected_old);
    puts("PASS: 100 successive interruptions preserve progress and boot trials");

    /* Three unconfirmed application boots trigger a recoverable reverse swap. */
    assert(prepareFirmwareBoot(&items) && items.boot_count == 2);
    assert(prepareFirmwareBoot(&items) && items.boot_count == 3);
    assert(prepareFirmwareBoot(&items));
    assert(memcmp((const void*)FW1_BANK_ADDRESS, expected_old, FW_SIZE) == 0);
    items.app_valid = 1;
    items.boot_count = 0;
    assert(nviUpdate(NVITEMS_ADDRESS, &items));
    assert(prepareFirmwareBoot(&items) && items.app_valid == 1 && items.boot_count == 0);
    puts("PASS: failed-boot fallback and application confirmation persist");

    /* Reject corrupt downloads before any erase. */
    memcpy(FLASH, initial_flash, FLASH_BYTES);
    ((uint8_t*)FW2_APP_ADDRESS)[100] ^= 1;
    items.active_app = FW2;
    unsigned previous_writes = writes;
    assert(!firmwareSwapStart(&items) && writes == previous_writes);

    /* Full copy completed, new image corrupt, old image still valid in FW2. */
    memcpy(FLASH, pending_final, FLASH_BYTES);
    ((uint8_t*)FW1_APP_ADDRESS)[100] ^= 1;
    checkpoint_count = 0;
    before_flash = capture;
    assert(prepareFirmwareBoot(&items));
    before_flash = NULL;
    assert(memcmp((const void*)FW1_BANK_ADDRESS, expected_old, FW_SIZE) == 0);
    testCuts("rollback of corrupt firmware", expected_old, NULL);

    free(checkpoints);
    munmap(FLASH, FLASH_BYTES);
    printf("PASS: %u simulated resets; production recovery code, flash-only restart state\n", reset_cases);
}
