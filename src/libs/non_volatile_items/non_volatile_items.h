#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

#define MAX_FAILED_BOOT_COUNTS 3
#define IMAGE_MAGIC 0xbeef

typedef enum Firmware
{
    FW_NONE = 0,
    FW1 = 1,
    FW2 = 2,
    BOOT_MODE = 11
} Firmware;

typedef struct BinaryVersion
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} __attribute__((packed, aligned(1))) BinaryVersion;

typedef struct NonVolatileItems
{
    BinaryVersion bootloader_version;
    uint8_t boot_count;
    uint8_t app_valid;
    uint8_t active_app;
} __attribute__((packed, aligned(1))) NonVolatileItems;

bool isMaxBootCount(const NonVolatileItems* nv_items);
bool isAppValid(const NonVolatileItems* nv_items);
bool isActiveFirmwareSelected(const NonVolatileItems* nv_items);
void increaseActiveApp(NonVolatileItems* nv_items);
void increaseBootCount(NonVolatileItems* nv_items);

/**
 * @brief Checks if firmware 1 is active
 */
bool isFirmware1Active(const NonVolatileItems* nv_items);

/**
 * @brief Checks if firmware 2 is active
 */
bool isFirmware2Active(const NonVolatileItems* nv_items);

/**
 * @brief Set active app to given value and reset app_valid and boot_count
 */
void setActiveApp(NonVolatileItems* nv_items, uint8_t active_app);

/**
 * @brief Checks if boot mode is active, i.e. no firmware should be loaded.
 *
 * @return true boot mode is active, do not load firmware
 * @return false boot mode not active
 */
bool isBootModeActive(const NonVolatileItems*);

/**
 * @brief Store NonVolatileItems data to flash.
 * @param nvi_address address of NV items
 * @param nvi struct of NonVolatileItems
 *
 * @return true if store operation succeded, false otherwise
 */
bool nviStore(uint32_t nvi_address, const NonVolatileItems* nvi);

/**
 * @brief Load NonVolatileItems data from flash.
 *
 * @return filled struct of NonVolatileItems
 */
/* False means damaged metadata; callers must not select/confirm an image. */
bool nviLoad(uint32_t nvi_address, NonVolatileItems* nvi);

/**
 * @brief Store nvi if differs from nvi on flash
 */
bool nviUpdate(uint32_t nvi_address, const NonVolatileItems* nvi_to_update);

#ifdef __cplusplus
}
#endif  // __cplusplus
