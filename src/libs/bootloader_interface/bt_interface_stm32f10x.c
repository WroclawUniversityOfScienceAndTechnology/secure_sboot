#include <circularuart.h>
#include <flash.h>
#include <led_interface.h>
#include <stm32f1xx_hal.h>
#include <at.h>
#include <generated/memory_map.h>
#include <platform.h>
#include <string.h>

#include "bt_interface.h"

#define BAUD_RATE 115200U

// Temporary UART TX check: set to 0 or comment out this define to disable.
#define UART_TX_DEBUG 0

/**
 * @brief  Initialize clk amd interrupt table
 * @return None
 */
static void nvicVectorTableUpdate(uint32_t offset)
{
    // Set the Vector Table base address offset
    SCB->VTOR = FLASH_BASE + offset;
    __DSB();
    __ISB();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

void initAll()
{
    platformInit(BOOTLOADER_ADDRESS);
    ledInit();
    atInitUartBuffers();
}

static void drainUart(void)
{
    while (CircularUART_GetUnsentCount() != 0) {}
    while ((USART1->SR & USART_SR_TC) == 0) {}
}

void bootLog(const char* message)
{
    circularUartSend((const uint8_t*)message, strlen(message));
    drainUart();
}

void deinitAll(uint32_t nvic_vector_table_offset)
{
    drainUart();
    __disable_irq();
    USART1->CR1 = 0;
    __HAL_RCC_USART1_FORCE_RESET();
    __HAL_RCC_USART1_RELEASE_RESET();
    __HAL_RCC_USART1_CLK_DISABLE();
    ledDeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    nvicVectorTableUpdate(nvic_vector_table_offset);
}

typedef void (*pFunction)();

void jumpToAddress(uint32_t fw_address, uint32_t nvic_vector_offset)
{
    uint32_t jump_address = *(volatile uint32_t*)(fw_address + nvic_vector_offset);
    uint32_t stack = *(volatile uint32_t*)fw_address;
    /* No C code may access the bootloader stack after MSP changes. */
    __asm volatile("msr msp, %0\n"
                   "movs r2, #0\n"
                   "msr control, r2\n"
                   "isb\n"
                   "cpsie i\n"
                   "bx %1\n" : : "r"(stack), "r"(jump_address) : "r2", "memory");
    __builtin_unreachable();
}

void enterBootMode()
{
    bootLog("BL BOOT MODE\r\n");
    ledToggle(true);
#if defined(UART_TX_DEBUG) && UART_TX_DEBUG
    uint32_t last_debug_tx = HAL_GetTick() - 1000U;
#endif
    while (true)
    {
        /** This is where boot didn't find the right
         * firmware to jump-to.
         * indicated by the blink of led
         */
        ledRunCyclic();
        atPoolUartBuffer();
#if defined(UART_TX_DEBUG) && UART_TX_DEBUG
        const uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last_debug_tx) >= 1000U)
        {
            static const uint8_t message[] = "OK\r\n";
            // Exercise UART TX without requiring any received AT command.
            circularUartSend(message, sizeof(message) - 1);
            last_debug_tx = now;
        }
#endif
    }
}
