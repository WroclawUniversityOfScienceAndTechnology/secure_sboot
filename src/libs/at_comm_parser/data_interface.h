#pragma once

#ifndef _DATA_INTERFACE_H
#define _DATA_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Buffers setting.
#define TX_BUFFERSIZE_UART_2N 7UL  // Buffer size of 2^7 = 128 bytes (127 usable).
#define RX_BUFFERSIZE_UART_2N 11UL  // Buffer size of 2^11 = 2048byte,
#define TX_BUFFERSIZE (1UL << TX_BUFFERSIZE_UART_2N)
#define RX_BUFFERSIZE (1UL << RX_BUFFERSIZE_UART_2N)
#define RESPONSE_BUFFER_SIZE 1088

    typedef int8_t Result_t;
#define ERROR_T (-1)
#define INPROGRESS_T (0)
#define SUCCESS_T (1)

typedef enum
{
    PARITY_NONE = 0x00,
    PARITY_ODD = 0x01,
    PARITY_EVEN = 0x02,
} DataParity;
#define DEFAULT_PARITY PARITY_NONE

void dataInterfaceInit(const uint32_t baudrate);
// Foreground only, with UART/SysTick interrupts enabled. Returns bytes queued;
// may be short after one second without progress. Does not wait for wire idle.
uint16_t dataInterfaceSend(const uint8_t* data, const uint16_t data_length);
// Returns one AT frame without CRLF; message_size is zero unless successful.
// Oversized frames return ERROR_T after their CRLF; subsequent frames survive.
Result_t dataInterfaceRead(uint8_t* const data, const uint16_t data_length, uint16_t* const message_size);
void dataInterfaceClearBuffers(bool rx_clear, bool tx_clear);

// Same enqueue and timeout semantics as dataInterfaceSend.
uint16_t dataSimpleSend(const uint8_t* data, const uint16_t data_length);

#ifdef __cplusplus
}
#endif  // __cplusplus
#endif
