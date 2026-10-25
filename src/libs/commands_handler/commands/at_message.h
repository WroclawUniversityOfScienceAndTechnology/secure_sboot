#pragma once

#ifndef _MESSAGE_HEADER
#define _MESSAGE_HEADER

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Header of the message transferred between client and the driver
 */

#define MAX_AT_COMMAND_LEN 10

typedef struct
{
    // Length of the message payload, max message length is 64kB
    char command[MAX_AT_COMMAND_LEN];
    uint8_t command_len;
    uint16_t data_size;
    char* data;
} AtMessage;

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
