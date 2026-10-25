#pragma once

#ifndef _CMD_DFU_H
#define _CMD_DFU_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include "at_message.h"

/**
 * @brief Data Firmware update (DFU)
 */
bool cmdDfuOpen(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);
bool cmdDfuWrite(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);
bool cmdDfuClose(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
