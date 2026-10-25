#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdint.h>
#include <stdbool.h>

bool firmwareImageCheck(uint32_t bank_addr);

#define FIRMWARE_HEADER_SIZE 0x200
#define MD5_SIZE 16
#define GIT_SHA_SIZE 8
#define IMAGE_MAGIC 0xbeef

typedef struct NonVolatileItems NonVolatileItems;

typedef enum FirmwaresImageStatus
{
    NONE_VALID = 0,
    FW1_VALID = 1,
    FW2_VALID = 2,
    ALL_VALID = 3,
} FirmwaresImageStatus;

typedef struct FirmwareDetails
{
    uint32_t data_length;
    uint32_t data_offset;
    uint8_t md5_sign[MD5_SIZE];
    uint16_t image_magic;
    uint8_t image_type;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint32_t vector_addr;
    uint32_t reserved;
    uint8_t git_sha[GIT_SHA_SIZE];
} __attribute__((packed, aligned(1))) FirmwareDetails;

typedef struct FirmwareHeader
{
    FirmwareDetails firmware_details;
    uint8_t padding[FIRMWARE_HEADER_SIZE - sizeof(FirmwareDetails)];
} FirmwareHeader;

/**
 * @brief Select active firmware
 *
 * @param nv_items Pointer to NV items
 * @param firmwares_Md5_status firmwares md5 status
 * @param fw1_details fw1 details
 * @param fw2_details fw2 details
 */
void selectActiveFirmware(NonVolatileItems* nv_items, FirmwaresImageStatus firmwares_Md5_status,
                          const FirmwareDetails* fw1_details, const FirmwareDetails* fw2_details);

#ifdef __cplusplus
}
#endif  // __cplusplus