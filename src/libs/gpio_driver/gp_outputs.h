#pragma once

#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void outputsInit();
void setOutputState(uint8_t output_no, bool level);


#ifdef __cplusplus
}
#endif // __cplusplus
