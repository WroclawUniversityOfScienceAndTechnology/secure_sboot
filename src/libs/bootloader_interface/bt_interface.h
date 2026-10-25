#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>
#include <non_volatile_items.h>

/**
 * @brief  Initialize the main system element such as clk,
 * interrupt table for bootloader
 * @return None
 */
void initAll();

/* Send a boot status line and drain UART before continuing (including flash writes). */
void bootLog(const char* message);

/**
 * @brief  Deinitialize the main system element
 * @return None
 */
void deinitAll(uint32_t nvic_vector_table_offset);

/**
 * @brief  Jump to firmware
 * @param  fw_address Firmware address
 * @param  nvic_vector_offset NVIC vector offset
 * @return None
 */
void jumpToAddress(uint32_t fw_address, uint32_t nvic_vector_offset);

/**
 * Start an exchange of the fixed FW1/FW2 slots, or resume a recorded exchange.
 * On failure preserve the journal; do not select an image or allow DFU over
 * a pending exchange. Both backups and all progress are persistent.
 */
bool firmwareSwapStart(const NonVolatileItems* selection);
bool firmwareSwapResume(void);
/* Recover before validation, select, then durably record this boot attempt. */
bool prepareFirmwareBoot(NonVolatileItems* items);

/**
 * @brief  Infinite loop
 */
void enterBootMode();

#ifdef __cplusplus
}
#endif  // __cplusplus
