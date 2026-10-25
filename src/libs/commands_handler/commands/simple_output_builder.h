#pragma once

#ifndef _SIMPLE_OUTPUT_BUILDER_H
#define _SIMPLE_OUTPUT_BUILDER_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

#define AT_SIZE 2

    /**
     * @brief     prepare the output driver message, do not change the command type - should be rewritten
     * @param message_out
     * @param message_out_size
     * @param status
     * @return true in case of success, false otherwise
     */
    bool buildSimpleOutputMessage(uint8_t* message_out, uint16_t* message_out_size, bool status);
    uint8_t hexDecode(const char* in);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
