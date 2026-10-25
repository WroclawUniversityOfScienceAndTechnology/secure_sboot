// clang-format off
#include "validity.h"
#include "md5.h"

#include <string.h>
// clang-format on

/**
 * @brief Calculate MD5 hash value of firmware
 *
 * @param mem_verify Pointer to structure type of MemVerifyStruct
 */
static void calculateHash(MemVerifyStruct* mem_verify)
{
    MD5_CTX md5;
    MD5Init(&md5);
    MD5Update(&md5, (unsigned char*)&mem_verify->image_magic, sizeof(mem_verify->image_magic));
    MD5Update(&md5, (unsigned char*)mem_verify->firmware, mem_verify->firmware_size);
    MD5Final(&md5, (unsigned char*)mem_verify->calculated_md5);
}

/**
 * @brief Verify MD5 signature - compare read from packet with calculated
 *
 * @param mem_verify Pointer to structure type of MemVerifyStruct
 * @return true if md5 signatures are equal
 * @return false if signatures are not equal
 */
static bool verifyMD5(const MemVerifyStruct* mem_verify)
{
    return memcmp(mem_verify->calculated_md5, mem_verify->md5_sign, MD5_SIZE) == 0;
}

bool validateMD5(MemVerifyStruct *mem_verify)
{
    if (mem_verify->firmware == NULL)
    {
        return false;
    }

    memset(mem_verify->calculated_md5, 0x00, MD5_SIZE);
    calculateHash(mem_verify);
    return verifyMD5(mem_verify);
}
