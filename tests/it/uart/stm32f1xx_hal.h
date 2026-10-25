#pragma once

// Minimal HAL/register model for the real UART driver in the host tests.
#include <assert.h>
#include <stdint.h>

typedef struct { uint32_t SR, DR, CR1; } USART_TypeDef;
extern USART_TypeDef test_usart;
#define USART1 (&test_usart)
#define USART1_IRQn 37
#define GPIOA ((void*)1)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_AF_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define UART_WORDLENGTH_8B 0U
#define UART_STOPBITS_1 0U
#define UART_PARITY_NONE 0U
#define UART_PARITY_ODD 1U
#define UART_PARITY_EVEN 2U
#define UART_MODE_TX_RX 12U
#define UART_HWCONTROL_NONE 0U
#define UART_OVERSAMPLING_16 0U
#define UART_IT_TXE (1U << 7)
#define UART_IT_RXNE (1U << 5)
#define UART_FLAG_TXE (1U << 7)
#define UART_FLAG_RXNE (1U << 5)
#define UART_FLAG_ORE (1U << 3)
#define HAL_OK 0
#define assert_param(expr) assert(expr)

typedef struct {
    uint32_t Pin, Mode, Pull, Speed;
} GPIO_InitTypeDef;
typedef struct {
    uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
} UART_InitTypeDef;
typedef struct {
    USART_TypeDef *Instance;
    UART_InitTypeDef Init;
} UART_HandleTypeDef;

extern uint32_t test_primask;
extern unsigned test_remap_disabled, test_barriers, test_irq_restores;
#define __get_PRIMASK() test_primask
#define __disable_irq() ((void)(test_primask = 1))
#define __set_PRIMASK(value) test_restore_irq(value)
#define __DMB() test_memory_barrier()
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)
#define __HAL_AFIO_REMAP_USART1_DISABLE() ((void)(test_remap_disabled = 1))
#define __HAL_UART_ENABLE_IT(uart, it) ((uart)->Instance->CR1 |= (it))
#define __HAL_UART_DISABLE_IT(uart, it) ((uart)->Instance->CR1 &= ~(it))
#define __HAL_UART_GET_FLAG(uart, flag) ((uart)->Instance->SR & (flag))
#define __HAL_UART_GET_IT_SOURCE(uart, it) ((uart)->Instance->CR1 & (it))
#define __HAL_UART_CLEAR_OREFLAG(uart) ((uart)->Instance->SR &= ~(UART_FLAG_ORE | UART_FLAG_RXNE))

void test_memory_barrier(void);
void test_restore_irq(uint32_t value);
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
int HAL_UART_Init(UART_HandleTypeDef *uart);
void HAL_NVIC_SetPriority(int irq, unsigned priority, unsigned subpriority);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_DisableIRQ(int irq);
uint32_t HAL_GetTick(void);
