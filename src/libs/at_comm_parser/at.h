#pragma once

#define BAUDRATE 115200

#ifdef __cplusplus
extern "C" {
#endif

void atInitUartBuffers();
void atPoolUartBuffer();

#ifdef __cplusplus
}
#endif
