#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

#define HEADER_LEN_SIZE 512
#define MD5_SIZE 16

typedef struct
{
    uint8_t* firmware;
    uint32_t firmware_size;
    uint8_t* md5_sign;
    uint8_t calculated_md5[MD5_SIZE];
    uint16_t image_magic;
} MemVerifyStruct;

/**
 * @brief Checks validity of signed firmware packet calculating the md5
 *  and comparing it with md5 provided with the packet.

 * @return true if calculated md5 is the same as the one provided with packet
 * @return false if calculated md5 is different to the one provided with packet
 */
bool validateMD5(MemVerifyStruct* mem_verify);

#ifdef __cplusplus
}
#endif  // __cplusplus
