

#pragma once

#ifndef _COMMAND_HANDLER_H_
#define _COMMAND_HANDLER_H_

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

bool commandHandler(const uint8_t *message, const uint16_t message_size, uint8_t *message_out, uint16_t *message_out_size);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
