#include "flash_mock.h"
#include <assert.h>
#include <flash.h>
#include <generated/memory_map.h>
#include <stddef.h>
#include <string.h>

unsigned writes;
unsigned fail_write;
unsigned swap_starts, integrity_rejections, fw1_returns;
void (*before_flash)(enum FlashOperation, uint32_t, const uint32_t*, uint32_t);

uint32_t getPageSize(void) { return 1024; }
bool flashRead(uint32_t address, uint32_t* data, uint32_t words)
{
    memcpy(data, (const void*)(uintptr_t)address, words * 4);
    return true;
}
bool flashErasePage(uint32_t address)
{
    assert(address >= NVITEMS_ADDRESS && address + 1024 <= 0x08010000);
    assert(address % 1024 == 0);
    if (before_flash)
        before_flash(ERASE_PAGE, address, NULL, 0);
    if (++writes == fail_write)
        return false;
    memset((void*)(uintptr_t)address, 0xff, 1024);
    return true;
}
bool flashProgram(uint32_t address, const uint32_t* data, uint32_t words)
{
    assert(address >= NVITEMS_ADDRESS && address + words * 4 <= 0x08010000);
    assert(address % 4 == 0 && words > 0 && words <= (1024 - address % 1024) / 4);
    if (before_flash)
        before_flash(PROGRAM_WORDS, address, data, words);
    if (++writes == fail_write)
        return false;
    for (uint32_t i = 0; i < words; ++i)
    {
        assert(((const uint32_t*)(uintptr_t)address)[i] == UINT32_MAX);
        memcpy((void*)(uintptr_t)(address + i * 4), data + i, 4);
    }
    return true;
}
bool flashEraseAndProgram(uint32_t address, const uint32_t* data, uint32_t words)
{
    return flashErasePage(address) && flashProgram(address, data, words);
}

void bootLog(const char* message)
{
    swap_starts += strcmp(message, "BL SWAP START\r\n") == 0;
    integrity_rejections += strcmp(message, "BL FW2 REJECTED INTEGRITY\r\n") == 0;
    fw1_returns += strcmp(message, "BL RETURN FW1\r\n") == 0;
}
