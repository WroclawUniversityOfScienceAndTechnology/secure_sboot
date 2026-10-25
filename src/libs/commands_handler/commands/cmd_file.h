#pragma once

#ifndef _CMD_FOPEN_H
#define _CMD_FOPEN_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include "at_message.h"

    /**
     * @brief Open file/buffer for writting
     * @param at_message - input message
     * @param message_out - output message which shall be set
     * @param message_out_size - output message size which shall be set
     * @return true in case of success, false otherwise
     */
    bool cmdFOpen(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

    /**
     * @brief Write to open file
     * @param at_message - input message
     * @param message_out - output message which shall be set
     * @param message_out_size - output message size which shall be set
     * @return true in case of success, false otherwise
     */
    bool cmdFWrite(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

    /**
     * @brief Close opened file
     * @param at_message - input message
     * @param message_out - output message which shall be set
     * @param message_out_size - output message size which shall be set
     * @return true in case of success, false otherwise
     */
    bool cmdFClose(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
