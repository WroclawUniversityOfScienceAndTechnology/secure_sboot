#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>
#include <stdint.h>

#define UART2_CHANNEL USART2
#define UART2_PIN_RX GPIO_Pin_2
#define UART2_PIN_TX GPIO_Pin_3
#define UART2_BAUD_RATE 115200U

/*Re-try counter for sedn/receive error*/
#define UART_RETRY_COUNTER 100

void uartInit();
void uartDeinit();

bool uartWriteByte(uint8_t* buffer);
bool uartWriteBuffer(uint8_t* buffer, uint32_t size);

bool uartReadByte(uint8_t* buffer);
bool uartReadBuffer(uint8_t* buffer, uint32_t size);

#ifdef __cplusplus
}
#endif  // __cplusplus
