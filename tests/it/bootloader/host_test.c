#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <generated/memory_map.h>
#include <bt_interface.h>
#include <firmware_details.h>
#include <non_volatile_items.h>
#include <nv_journal.h>
#include <flash.h>
#include "commands/cmd_dfu.h"
#include "flash_mock.h"

static void load(const char* path, uint32_t address)
{
    FILE* file = fopen(path, "rb");
    assert(file);
    assert(fread((void*)(uintptr_t)address, 1, FW_SIZE, file) > FW_OFFSET);
    fclose(file);
}
static void dfu(bool (*command)(const AtMessage*, uint8_t*, uint16_t*),
                char* data, bool expected)
{
    AtMessage message = {.data = data, .data_size = strlen(data)};
    uint8_t response[1088] = {0};
    uint16_t size;
    command(&message, response, &size);
    const char* status = expected ? "OK\n" : "ERROR\n";
    assert(size >= strlen(status) && size < sizeof(response));
    assert(memcmp(response + size - strlen(status), status, strlen(status)) == 0);
}

static void transfer(const char* path, bool corrupt)
{
    FILE* transcript = fopen(path, "r");
    assert(transcript);
    char line[1089];
    while (fgets(line, sizeof(line), transcript))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "AT+DFUOP", 8) == 0)
            dfu(cmdDfuOpen, line + 8, true);
        else if (strncmp(line, "AT+DFUWR", 8) == 0)
            dfu(cmdDfuWrite, line + 8, true);
        else
        {
            assert(strncmp(line, "AT+DFUCL", 8) == 0);
            if (corrupt)
                ((uint8_t*)FW2_BANK_ADDRESS)[FW_OFFSET + 0x100] ^= 1;
            dfu(cmdDfuClose, line + 8, true);  // Transport close is not activation.
        }
    }
    fclose(transcript);
}
int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 5);
    void* memory = mmap((void*)0x08000000, 65536, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(memory == (void*)0x08000000);
    memset(memory, 0xff, 65536);
    load(argv[1], FW1_BANK_ADDRESS);
    load(argv[2], FW2_BANK_ADDRESS);
    assert(firmwareImageCheck(FW1_BANK_ADDRESS));
    assert(firmwareImageCheck(FW2_BANK_ADDRESS));
    FirmwareDetails* header = (void*)FW2_BANK_ADDRESS;
    uint32_t length = header->data_length;
    header->data_length = UINT32_MAX;
    assert(!firmwareImageCheck(FW2_BANK_ADDRESS));
    header->data_length = length;
    header->data_offset = 0;
    assert(!firmwareImageCheck(FW2_BANK_ADDRESS));
    header->data_offset = FW_OFFSET;
    uint32_t* vector = (void*)FW2_APP_ADDRESS;
    uint32_t reset = vector[1];
    vector[1] = FW2_APP_ADDRESS | 1;
    assert(!firmwareImageCheck(FW2_BANK_ADDRESS));
    vector[1] = reset;
    ((uint8_t*)FW2_APP_ADDRESS)[20] ^= 1;
    assert(!firmwareImageCheck(FW2_BANK_ADDRESS));
    NonVolatileItems invalid_selection = {.active_app = FW2};
    unsigned before_validation = writes;
    assert(!firmwareSwapStart(&invalid_selection));
    assert(writes == before_validation);  // Not even a transaction intent was written.
    ((uint8_t*)FW2_APP_ADDRESS)[20] ^= 1;
    NonVolatileItems nv = {.active_app = FW_NONE};
    selectActiveFirmware(&nv, ALL_VALID, (void*)FW1_BANK_ADDRESS, header);
    assert(isFirmware2Active(&nv));
    uint8_t original[2 * FW_SIZE];
    memcpy(original, (void*)FW1_BANK_ADDRESS, sizeof(original));
    assert(firmwareSwapStart(&nv));
    assert(memcmp((void*)FW1_BANK_ADDRESS, original + FW_SIZE, FW_SIZE) == 0);
    assert(memcmp((void*)FW2_BANK_ADDRESS, original, FW_SIZE) == 0);
    assert(firmwareImageCheck(FW1_BANK_ADDRESS));
    dfu(cmdDfuOpen, "=2", true);
    dfu(cmdDfuWrite, "=4,ABCD", true);
    unsigned before = writes;
    dfu(cmdDfuWrite, "=22", false);
    dfu(cmdDfuWrite, "=65536", false);
    dfu(cmdDfuWrite, "=-1", false);
    assert(writes == before);
    dfu(cmdDfuWrite, "=0", true);
    assert(*(uint16_t*)FW2_BANK_ADDRESS == 0xcdab);
    for (unsigned i = 2; i < 1024; ++i)
        assert(((uint8_t*)FW2_BANK_ADDRESS)[i] == 0xff);
    dfu(cmdDfuWrite, "=2,GG", false);
    dfu(cmdDfuClose, "", true);
    dfu(cmdDfuWrite, "=2,AA", false);
    dfu(cmdDfuClose, "", true);
    dfu(cmdDfuOpen, "=2", true);
    for (unsigned i = 0; i < 1024; ++i)
        dfu(cmdDfuWrite, "=2,AA", true);
    dfu(cmdDfuWrite, "=2,AA", false);
    dfu(cmdDfuClose, "", true);
    dfu(cmdDfuOpen, "=2", true);
    dfu(cmdDfuClose, "", true);  // An empty readiness probe is not a candidate.
    if (argc == 5)
    {
        /* Replay the Python uploader's actual commands through the C DFU
         * handlers, then let the bootloader validate the transferred ARM image. */
        memset(memory, 0xff, 65536);
        load(argv[3], FW1_BANK_ADDRESS);
        assert(firmwareImageCheck(FW1_BANK_ADDRESS));
        transfer(argv[4], false);
        assert(memcmp((void*)FW1_BANK_ADDRESS, (void*)FW2_BANK_ADDRESS, FW_SIZE) == 0);
        assert(firmwareImageCheck(FW2_BANK_ADDRESS));
        puts("PASS: built ARM image uploaded through C DFU handlers and accepted by bootloader");
        uint8_t preserved[FW_SIZE];
        memcpy(preserved, (void*)FW1_BANK_ADDRESS, FW_SIZE);
        NonVolatileItems selected;
        /* The exact same bootloader gate must protect downloads performed in
         * boot mode and those performed while FW1 is running. */
        const uint8_t modes[] = {BOOT_MODE, FW1};
        for (unsigned mode = 0; mode < sizeof(modes); ++mode)
        {
            memset((void*)NVITEMS_ADDRESS, 0xff, NVITEMS_SIZE);
            memset(&selected, 0, sizeof(selected));
            selected.active_app = modes[mode];
            assert(nviStore(NVITEMS_ADDRESS, &selected));
            transfer(argv[4], true);
            assert(!firmwareImageCheck(FW2_BANK_ADDRESS));
            assert(nviLoad(NVITEMS_ADDRESS, &selected) && selected.active_app == modes[mode]);
            setActiveApp(&selected, FW2);  // AT+RAA=2, then reboot into bootloader.
            assert(nviUpdate(NVITEMS_ADDRESS, &selected));
            unsigned starts_before = swap_starts;
            unsigned rejections_before = integrity_rejections;
            unsigned returns_before = fw1_returns;
            assert(prepareFirmwareBoot(&selected) && selected.active_app == FW1);
            assert(swap_starts == starts_before);
            assert(integrity_rejections == rejections_before + 1);
            assert(fw1_returns == returns_before + 1);
            assert(memcmp(preserved, (void*)FW1_BANK_ADDRESS, FW_SIZE) == 0);
            NvState rejected;
            assert(nvStateLoad(&rejected) && rejected.phase == SWAP_IDLE);
        }
        puts("PASS: corrupted downloads from boot/app modes rejected before swap; FW1 preserved and selected");
        setActiveApp(&selected, FW2);
        assert(nviUpdate(NVITEMS_ADDRESS, &selected));
        fail_write = writes + 1;
        assert(!prepareFirmwareBoot(&selected));
        fail_write = 0;
        assert(memcmp(preserved, (void*)FW1_BANK_ADDRESS, FW_SIZE) == 0);
        puts("PASS: NV selection write failure prevents application handoff");
        /* If neither bank is valid, selection must remain in boot mode. */
        memset((void*)FW1_BANK_ADDRESS, 0xff, FW_SIZE);
        setActiveApp(&selected, FW2);
        assert(nviUpdate(NVITEMS_ADDRESS, &selected));
        assert(prepareFirmwareBoot(&selected) && selected.active_app == BOOT_MODE);
        puts("PASS: rejected update with no valid FW1 stays in boot mode");

        /* A checksum-valid application can still fail at runtime. Transfer
         * the actual ARM fixture, emulate its unconfirmed reboots, and verify
         * both swaps and the original image's identity using the real policy. */
        memset(memory, 0xff, 65536);
        load(argv[1], FW1_BANK_ADDRESS);
        memcpy(preserved, (void*)FW1_BANK_ADDRESS, FW_SIZE);
        transfer(argv[4], false);
        uint8_t unconfirmed[FW_SIZE];
        memcpy(unconfirmed, (void*)FW2_BANK_ADDRESS, FW_SIZE);
        assert(firmwareImageCheck(FW2_BANK_ADDRESS));
        memset(&selected, 0, sizeof(selected));
        selected.active_app = FW2;
        assert(nviStore(NVITEMS_ADDRESS, &selected));
        unsigned starts_before = swap_starts;
        unsigned rejections_before = integrity_rejections;
        for (unsigned trial = 1; trial <= MAX_FAILED_BOOT_COUNTS; ++trial)
        {
            assert(prepareFirmwareBoot(&selected));
            assert(selected.boot_count == trial && selected.app_valid == 0);
            assert(memcmp((void*)FW1_BANK_ADDRESS, unconfirmed, FW_SIZE) == 0);
            assert(memcmp((void*)FW2_BANK_ADDRESS, preserved, FW_SIZE) == 0);
            assert(swap_starts == starts_before + 1);
        }
        assert(prepareFirmwareBoot(&selected));
        assert(swap_starts == starts_before + 2);
        assert(integrity_rejections == rejections_before);
        assert(memcmp((void*)FW1_BANK_ADDRESS, preserved, FW_SIZE) == 0);
        assert(memcmp((void*)FW2_BANK_ADDRESS, unconfirmed, FW_SIZE) == 0);
        assert(firmwareImageCheck(FW1_BANK_ADDRESS) && firmwareImageCheck(FW2_BANK_ADDRESS));
        assert(isFirmware1Active(&selected) && selected.boot_count == 1);
        selected.app_valid = 1;
        selected.boot_count = 0;
        assert(nviUpdate(NVITEMS_ADDRESS, &selected));
        assert(prepareFirmwareBoot(&selected) && selected.app_valid == 1 && selected.boot_count == 0);
        assert(swap_starts == starts_before + 2);
        puts("PASS: checksum-valid ARM image accepted and swapped; unconfirmed boot trials restore original image");
    }
    /* A legacy application erases journal A when confirming startup. That
     * resurrects an older pending record from B, even after SWAP DONE. Reproduce
     * the resulting page=22/READY lockout after writing a corrupt candidate. */
    memset(memory, 0xff, 65536);
    load(argv[1], FW1_BANK_ADDRESS);
    load(argv[2], FW2_BANK_ADDRESS);
    NonVolatileItems items = {.active_app = FW2};
    assert(nviStore(NVITEMS_ADDRESS, &items));
    assert(prepareFirmwareBoot(&items));
    NvState state;
    assert(nvStateLoad(&state) && state.phase == SWAP_IDLE);
    uint8_t consistent[65536];
    memcpy(consistent, memory, sizeof(consistent));
    items.app_valid = 1;
    items.boot_count = 0;
    uint32_t legacy[2] = {0xff, 0};
    memcpy(legacy, &items, sizeof(items));
    assert(flashEraseAndProgram(NVITEMS_ADDRESS, legacy, 2));
    assert(nvStateLoad(&state) && state.phase != SWAP_IDLE);
    assert(!nvUpdateAllowed());
    ((uint8_t*)FW2_BANK_ADDRESS)[FW_OFFSET + 0x100] ^= 1;
    assert(!prepareFirmwareBoot(&items));
    assert(nvStateLoad(&state) && state.page == FW_SIZE / 1024 && state.phase == SWAP_READY);
    assert(!nviUpdate(NVITEMS_ADDRESS, &items));
    /* The current application confirms through the journal and preserves IDLE. */
    memcpy(memory, consistent, sizeof(consistent));
    assert(nviUpdate(NVITEMS_ADDRESS, &items));
    assert(nvStateLoad(&state) && state.phase == SWAP_IDLE);
    assert(prepareFirmwareBoot(&items) && items.app_valid == 1);
    puts("PASS: legacy NV writes reproduce final-page recovery lockout; journal confirmation preserves completion");
    munmap(memory, 65536);
    puts("PASS: image validation, selection, persistent swap and DFU bounds");
}
