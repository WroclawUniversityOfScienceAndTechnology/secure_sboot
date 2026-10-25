#include "uart_interface.h"
#include <stm32f10x_usart.h>
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>

void uartInit()
{
    USART_InitTypeDef uart_init_struct;

    GPIO_InitTypeDef gpio_struct;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);

    // clock for gpioa
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);

    /* Configure GPIO pin  for for RX|TX pin of UART*/

    gpio_struct.GPIO_Pin = UART2_PIN_RX | UART2_PIN_TX;

    gpio_struct.GPIO_Speed = GPIO_Speed_2MHz;

    gpio_struct.GPIO_Mode = GPIO_Mode_AF_PP;

    GPIO_Init(GPIOA, &gpio_struct);

    // intitalize init structure for usart

    uart_init_struct.USART_BaudRate = UART2_BAUD_RATE;

    uart_init_struct.USART_WordLength = USART_WordLength_8b;

    uart_init_struct.USART_StopBits = USART_StopBits_1;

    uart_init_struct.USART_Parity = USART_Parity_No;

    uart_init_struct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    uart_init_struct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;

    USART_Cmd(UART2_CHANNEL, ENABLE);

    USART_Init(UART2_CHANNEL, &uart_init_struct);
}

void uartDeinit()
{
    USART_DeInit(UART2_CHANNEL);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,DISABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,DISABLE);
}

bool uartWriteByte(uint8_t* buffer)
{
    uint32_t retry_counter = UART_RETRY_COUNTER;

    while (USART_GetFlagStatus(UART2_CHANNEL, USART_FLAG_TXE) == RESET)
    {
        if (!retry_counter)
        {
            return false;
        }
        --retry_counter;
    }

    USART_SendData(UART2_CHANNEL, buffer[0]);

    while (USART_GetFlagStatus(UART2_CHANNEL, USART_FLAG_TC) == RESET)
    {
        if (!retry_counter)
        {
            return false;
        }
        --retry_counter;
    }

    return true;
}

bool uartWriteBuffer(uint8_t* buffer, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
    {
        if (!uartWriteByte(&buffer[i]))
        {
            return false;
        }
    }

    return true;
}

bool uartReadByte(uint8_t* buffer)
{
    uint16_t data;
    uint32_t retry_counter = UART_RETRY_COUNTER;

    while (USART_GetFlagStatus(UART2_CHANNEL, USART_FLAG_RXNE) == RESET)
    {
        if (!retry_counter)
        {
            return false;
        }
        --retry_counter;
    }

    data = USART_ReceiveData(UART2_CHANNEL);

    buffer[0] = data & 0xffU;

    return true;
}

bool uartReadBuffer(uint8_t* buffer, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i)
    {
        if (!uartReadByte(&buffer[i]))
        {
            return false;
        }
    }

    return true;
}
