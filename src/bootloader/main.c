#include <bt_interface.h>
#include <dev_logger.h>
#include <firmware_details.h>
#include <generated/bootloader_version.h>
#include <generated/memory_map.h>
#include <non_volatile_items.h>
#include <stdint.h>
#include <validity.h>

typedef void (*pFunction)();
static void sysDeinit(uint32_t app_addr);
static void setVersion(NonVolatileItems*);
static void jumpToFirmware(uint32_t app_addr);
static uint32_t getActiveFirmwareAppAddress(uint8_t active_app);
static void devLogSendNVMData(NonVolatileItems* nv);
static void logVerifiedFirmware(uint32_t bank_address);

int main()
{
    initAll();
    bootLog("BL START\r\n");

    NonVolatileItems nv_items;
    if (!prepareFirmwareBoot(&nv_items))
    {
        /* Do not overwrite a pending transaction with BOOT_MODE. */
        bootLog("BL ERROR RECOVERY\r\n");
        enterBootMode();
    }
    setVersion(&nv_items);

    devLogSendNVMData(&nv_items);

    if (!nviUpdate(NVITEMS_ADDRESS, &nv_items))
    {
        bootLog("BL ERROR NV\r\n");
        enterBootMode();
    }

    if (!isBootModeActive(&nv_items))
    {
        const uint32_t fw_app_address = getActiveFirmwareAppAddress(nv_items.active_app);

        /* Selection (and, when needed, the swap) validated this image. Report
         * its identity only after the boot selection has been persisted. */
        logVerifiedFirmware(fw_app_address - FW_OFFSET);
        DEV_LOG(LOG_TYPE_INFO, "Control will jump to  address : %x\n", fw_app_address);

        sysDeinit(fw_app_address);
        jumpToFirmware(fw_app_address);
    }

    DEV_LOG(LOG_TYPE_INFO, "No valid firmware found. Entering boot mode");
    enterBootMode();

    while (true)
    {
    }
}

static void logVerifiedFirmware(uint32_t bank_address)
{
    const FirmwareDetails* details = (const FirmwareDetails*)bank_address;
    static const char hex[] = "0123456789abcdef";
    char message[] = "BL FW VERIFIED 00000000000000000000000000000000\r\n";
    const uint32_t prefix_size = sizeof("BL FW VERIFIED ") - 1;
    for (uint32_t i = 0; i < MD5_SIZE; ++i)
    {
        message[prefix_size + 2 * i] = hex[details->md5_sign[i] >> 4];
        message[prefix_size + 2 * i + 1] = hex[details->md5_sign[i] & 0x0F];
    }
    bootLog(message);
}

/**
 * @brief  This function performs the jump to the firmware Memory
 * @details The function carries out the following operations:
 *  - Remap the system flash memory
 *  - Perform the jump
 */
void jumpToFirmware(uint32_t app_addr)
{
    jumpToAddress(app_addr, NVIC_VECTOR_OFFSET);  // Jump to firmware
}

/**
 * @brief deinit the perif activated in bootloader mode to avoid problem in
 * the FW
 * @details
 *  - De-initialize the clock and peripheral configuration
 *  - Stop the systick
 */
void sysDeinit(uint32_t app_addr)
{
    uint32_t nvic_vector_table_offset = app_addr - BOOTLOADER_ADDRESS;
    deinitAll(nvic_vector_table_offset);
}

/**
 * @brief Set the version in NV from generated bootloader version file
 *
 * @param nv_items Pointer to NV items
 */
void setVersion(NonVolatileItems* nv_items)
{
    nv_items->bootloader_version.major = getVersionMajor();
    nv_items->bootloader_version.minor = getVersionMinor();
    nv_items->bootloader_version.patch = getVersionPatch();
}

uint32_t getActiveFirmwareAppAddress(uint8_t active_app)
{
    if (active_app % 2 == FW1)
    {
        return FW1_APP_ADDRESS;
    }
    return FW2_APP_ADDRESS;
}

inline static void devLogSendNVMData(NonVolatileItems* nv)
{
    DEV_LOG(LOG_TYPE_INFO, "Boot Loader Version: %u.%u.%u\n",
                           nv->bootloader_version.major,
                           nv->bootloader_version.minor,
                           nv->bootloader_version.patch);
    DEV_LOG(LOG_TYPE_INFO, "Boot Count     : %u\n", nv->boot_count);
    DEV_LOG(LOG_TYPE_INFO, "Valid App      : %u\n", nv->app_valid);
    DEV_LOG(LOG_TYPE_INFO, "Active App     : %u\n", nv->active_app);
}
