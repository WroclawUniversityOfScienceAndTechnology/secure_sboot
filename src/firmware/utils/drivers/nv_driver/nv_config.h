

#pragma once

#include <stdint.h>
#include <stdbool.h>

//Define the STM32F10x FLASH Page Size depending on the used STM32 device
#define FLASH_PAGE_SIZE    ((uint16_t)0x400) //1024

#define BANK1_WRITE_START_ADDR ((uint32_t)0x8004000)
#define BANK1_WRITE_END_ADDR ((uint32_t)0x8005000)

typedef enum
{
	NV_BLOCK1 = 0,
	NV_BLOCK2 = 1,
	NV_BLOCK_MAX = 1
} NvBlocks;

bool nvConfigRead(const NvBlocks block, uint32_t *data, uint32_t size);
bool nvConfigEraseAndProgram(const NvBlocks block, const uint32_t *data, const uint32_t size);
