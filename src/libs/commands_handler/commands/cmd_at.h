#pragma once

#ifndef _CMD_AT_H
#define _CMD_AT_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include "at_message.h"

    bool cmdAt(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
