#include "firmware_details.h"
#include <non_volatile_items.h>

static bool isActiveFirmwareMd5Valid(const NonVolatileItems* nv_items, FirmwaresImageStatus firmwares_Md5_status)
{
    return (isFirmware1Active(nv_items) && (firmwares_Md5_status == FW1_VALID || firmwares_Md5_status == ALL_VALID)) ||
           (isFirmware2Active(nv_items) && (firmwares_Md5_status == FW2_VALID || firmwares_Md5_status == ALL_VALID));
}

/**
 * @brief Combine major, minor and patch version into one variable
 *
 * @param fw_details Pointer to fw details
 */
static uint32_t getFirmwareVersionAsU32(const FirmwareDetails* fw_details)
{
    return (fw_details->version_major << 16U) + (fw_details->version_minor << 8U) + fw_details->version_patch;
}

/**
 * @brief Select newer firmware or fw1 in case both have the same version
 *
 * @param fw1_details Pointer to fw1 details
 * @param fw2_details Pointer to fw2 details
 */
static Firmware getNewerFirmware(const FirmwareDetails* fw1_details, const FirmwareDetails* fw2_details)
{
    uint32_t fw1_version = getFirmwareVersionAsU32(fw1_details);
    uint32_t fw2_version = getFirmwareVersionAsU32(fw2_details);

    return fw1_version >= fw2_version ? FW1 : FW2;
}

void selectActiveFirmware(NonVolatileItems* nv_items, FirmwaresImageStatus firmwares_image_status,
                          const FirmwareDetails* fw1_details, const FirmwareDetails* fw2_details)
{
    if (firmwares_image_status == NONE_VALID)
    {
        setActiveApp(nv_items, BOOT_MODE);
        return;
    }

    if (!isActiveFirmwareSelected(nv_items) && firmwares_image_status == ALL_VALID)
    {
        setActiveApp(nv_items, getNewerFirmware(fw1_details, fw2_details));
    }

    while (!isBootModeActive(nv_items))
    {
        if (!isActiveFirmwareMd5Valid(nv_items, firmwares_image_status))
        {
            increaseActiveApp(nv_items);
        }
        else if (isAppValid(nv_items))
        {
            return;
        }
        else if (isMaxBootCount(nv_items))
        {
            increaseActiveApp(nv_items);
        }
        else
        {
            increaseBootCount(nv_items);
            return;
        }
    }
}
