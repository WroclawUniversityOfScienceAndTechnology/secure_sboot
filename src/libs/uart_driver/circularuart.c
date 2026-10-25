/**
 * @file      circularuart.c
 * @author    Atakan S.
 * @date      01/01/2019
 * @version   1.0
 * @brief     Full-duplex UART driver based on circular buffer.
 * //TODO      Move hardware related parts into seperate file.
 *
 * @copyright Copyright (c) 2018 Atakan SARIOGLU ~ www.atakansarioglu.com
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

// Includes.
#include "circularuart.h"
#include "circularbuffer.h"
#include "stm32f1xx_hal.h"

static UART_HandleTypeDef uart = {.Instance = USART1};

// Settings.
#ifndef IRQPRIORITY_USART1
#define IRQPRIORITY_USART1 0
#endif

// Variables.
static CircularBufferObject_t rxBufferObject, txBufferObject;

/*
 * @brief Initializes UART hardware with the given baud-rate.
 * @param baud The baud-rate to set.
 * @param parity Parity setting. 0 for no-parity, 1 for odd and 2 for even.
 */
void circularUartInit(const uint32_t baud, const uint8_t parity)
{
    // Reinitialization must not let the ISR use buffers being reset.
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    CircularBuffer_init(&rxBufferObject, NULL, 0);
    CircularBuffer_init(&txBufferObject, NULL, 0);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_UART_DISABLE_IT(&uart, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&uart, UART_IT_TXE);
    __HAL_AFIO_REMAP_USART1_DISABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    uart.Init.BaudRate = baud;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = !parity ? UART_PARITY_NONE :
                      (parity == 1 ? UART_PARITY_ODD : UART_PARITY_EVEN);
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&uart) != HAL_OK)
        while (1) {}
    HAL_NVIC_SetPriority(USART1_IRQn, IRQPRIORITY_USART1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/*
 * @brief Initializes and enables TX.
 * @param buffer Memory buffer to use for tx circular buffer.
 * @param length_2N Size of the buffer memory, i.e. 8 indicates 2^8=256 bytes.
 */
void circularUartStartTx(uint8_t * const buffer, const uint8_t length_2N)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    // Disable the USART1 transmit buffer empty interrupt.
    __HAL_UART_DISABLE_IT(&uart, UART_IT_TXE);

    // Initialize the buffer.
    CircularBuffer_init(&txBufferObject, buffer, length_2N);
    __set_PRIMASK(primask);
}

/*
 * @brief Initializes and enables RX.
 * @param buffer Memory buffer to use for rx circular buffer.
 * @param length_2N Size of the buffer memory, i.e. 8 indicates 2^8=256 bytes.
 */
void circularUartStartRx(uint8_t * const buffer, const uint8_t length_2N)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    //-- Disable the USART1 receive buffer not empty interrupt.
    __HAL_UART_DISABLE_IT(&uart, UART_IT_RXNE);

    // Initialize the buffer.
    CircularBuffer_init(&rxBufferObject, buffer, length_2N);

    //-- Clear the RXNE bit to prevent outdated data.
    __HAL_UART_CLEAR_OREFLAG(&uart);

    //-- Enable the USART1 receive buffer not empty interrupt.
    __HAL_UART_ENABLE_IT(&uart, UART_IT_RXNE);
    __set_PRIMASK(primask);
}

/*
 * @brief Clear the TX buffer and fault flag.
 */
void circularUartClearTx(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    //-- Disable the USART1 transmit buffer empty interrupt.
    __HAL_UART_DISABLE_IT(&uart, UART_IT_TXE);

    // Initialize the buffer.
    CircularBuffer_checkAndClearFault(&txBufferObject, true);
    __set_PRIMASK(primask);
}

/*
 * @brief Clear the RX buffer and fault flag.
 */
void circularUartClearRx(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    // Clear buffer and fault.
    CircularBuffer_checkAndClearFault(&rxBufferObject, true);
    __set_PRIMASK(primask);
}

/*
 * @brief Triggers transmission of data.
 * @param data Data to send.
 * @param maxlen The requested length for sending data.
 * @return Returns the actual length that was copied to the tx buffer.
 */
uint16_t circularUartSend(const uint8_t * data, const uint16_t maxlen)
{
    uint16_t result = CircularBuffer_pushBack(&txBufferObject, data, maxlen);

    // Trigger the first transmission if TX is idle.
    if (result != 0)
    {
        //-- Enable the USART1 transmit buffer empty interrupt.
        __HAL_UART_ENABLE_IT(&uart, UART_IT_TXE);
    }

    // Return result.
    return result;
}

/*
 * @brief Get the received data from rx buffer.
 * @param data Memory to write the received data.
 * @param maxlen The requested length for receiving data.
 * @return Returns the actual length that was copied from the rx buffer.
 */
uint16_t circularUartReceive(uint8_t * data, const uint16_t maxlen)
{
    return CircularBuffer_popFront(&rxBufferObject, data, maxlen);
}

/*
 * @brief Get the number of bytes that are still in tx buffer.
 * @return Returns the number of unsent bytes.
 */
uint16_t CircularUART_GetUnsentCount(void)
{
    return CircularBuffer_getUnreadSize(&txBufferObject);
}

/*
 * @brief Get the number of received bytes in the rx buffer.
 * @return Returns the number of unread bytes.
 */
uint16_t CircularUART_GetUnreadCount(void)
{
    return CircularBuffer_getUnreadSize(&rxBufferObject);
}

/*
 * @brief Interrupt handler for RX and TX operations.
 */
void USART1_IRQHandler(void)
{
    //-- Transmit buffer empty interrupt.
    if ((__HAL_UART_GET_FLAG(&uart, UART_FLAG_TXE) && __HAL_UART_GET_IT_SOURCE(&uart, UART_IT_TXE)))
    {
        // Pop from tx buffer (if available) to UART.
        uint8_t data;
        if (CircularBuffer_popFrontByte(&txBufferObject, &data))
        {
            USART1->DR = data;
        }
        else
        {
            //-- Disable the USART1 transmit buffer empty interrupt.
            __HAL_UART_DISABLE_IT(&uart, UART_IT_TXE);
        }
    }

    //-- Reception complete interrupt.
    if ((__HAL_UART_GET_FLAG(&uart, UART_FLAG_RXNE) && __HAL_UART_GET_IT_SOURCE(&uart, UART_IT_RXNE)))
    {
        // Push from UART to rx buffer.
        CircularBuffer_pushBackByte(&rxBufferObject, (uint8_t)USART1->DR);
    }
    if (__HAL_UART_GET_FLAG(&uart, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(&uart);

}
