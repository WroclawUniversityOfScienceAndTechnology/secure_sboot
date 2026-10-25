#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

uint32_t getPageSize();
bool flashErasePage(uint32_t address);
/* Program only erased words, without erasing adjacent journal records. */
bool flashProgram(uint32_t address, const uint32_t* data, uint32_t words);
bool flashEraseAndProgram(uint32_t dest_address, const uint32_t* data, uint32_t size);
bool flashRead(uint32_t source_address, uint32_t* data, uint32_t size);

#ifdef __cplusplus
}
#endif  // __cplusplus
