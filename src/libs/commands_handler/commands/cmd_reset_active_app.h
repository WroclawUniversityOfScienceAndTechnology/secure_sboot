#pragma once

#ifndef _CMD_SAA_H
#define _CMD_SAA_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include "at_message.h"

    /**
     * @brief Force device standby
     * @param at_message - input message
     * @param message_out - output message which shall be set
     * @param message_out_size - output message size which shall be set
     * @return true in case of success, false otherwise
     */
    bool cmdResetActiveApp(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
