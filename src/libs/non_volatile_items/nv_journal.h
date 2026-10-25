#pragma once

#include "non_volatile_items.h"

/* Each phase describes the LAST completed, verified copy. READY also records
 * transaction intent before either firmware bank can be changed. */
typedef enum SwapPhase
{
    SWAP_IDLE = 0,
    SWAP_READY,
    SWAP_OLD_SAVED,
    SWAP_BOTH_SAVED,
    SWAP_FW1_WRITTEN
} SwapPhase;

typedef enum SwapDirection
{
    SWAP_FORWARD = 1,
    SWAP_ROLLBACK = 2
} SwapDirection;

typedef struct NvState
{
    NonVolatileItems items;
    uint16_t reserved;
    uint32_t phase;
    uint32_t page;
    uint32_t direction;
    uint32_t next_active;
    uint32_t old_valid;
    uint32_t old_page_crc;
    uint32_t new_page_crc;
    uint8_t old_digest[16];
    uint8_t new_digest[16];
} NvState;

/* No RAM cache: every call can be the first call following a reset. */
bool nvStateLoad(NvState* state);
bool nvStateStore(const NvState* state);
bool nvUpdateAllowed(void);
uint32_t nvChecksum(const void* data, uint32_t size);
