#pragma once
#include <stdint.h>

enum FlashOperation { ERASE_PAGE, PROGRAM_WORDS };
extern unsigned writes;
extern unsigned fail_write;
extern unsigned swap_starts, integrity_rejections, fw1_returns;
extern void (*before_flash)(enum FlashOperation operation, uint32_t address,
                            const uint32_t* data, uint32_t words);
