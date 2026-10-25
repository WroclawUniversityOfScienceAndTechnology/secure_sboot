#include "firmware_details.h"
#include <generated/memory_map.h>
#include <validity.h>

bool firmwareImageCheck(uint32_t bank_addr)
{
    if (bank_addr != FW1_BANK_ADDRESS && bank_addr != FW2_BANK_ADDRESS)
        return false;
    const FirmwareDetails* firmware_details = (FirmwareDetails*)(bank_addr);

    if (firmware_details->image_magic != IMAGE_MAGIC ||
        firmware_details->data_offset != FW_OFFSET ||
        firmware_details->data_length < 8 ||
        firmware_details->data_length > FW_SIZE - FW_OFFSET)
    {
        return false;
    }

    /* Both stored images execute at FW1_APP_ADDRESS in single-bank mode. */
    const uint32_t* vectors = (const uint32_t*)(bank_addr + FW_OFFSET);
    if (vectors[0] <= 0x20000000U || vectors[0] > 0x20005000U ||
        (vectors[0] & 7U) != 0 || (vectors[1] & 1U) == 0 ||
        (vectors[1] & ~1U) < FW1_APP_ADDRESS ||
        (vectors[1] & ~1U) >= FW1_APP_ADDRESS + firmware_details->data_length)
        return false;

    MemVerifyStruct mem_verify = {.firmware = (void*)(bank_addr + firmware_details->data_offset),
                                  .firmware_size = firmware_details->data_length,
                                  .md5_sign = (uint8_t*)firmware_details->md5_sign,
                                  .image_magic = firmware_details->image_magic};

    return validateMD5(&mem_verify);
}

