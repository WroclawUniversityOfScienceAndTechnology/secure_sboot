#include "sys_init.h"
#include <stm32f10x.h>
#include <misc.h>

/**
 * @brief Initialize clk amd interrupt table
 * @param offset - This value must be a multiple of 0x200.
 * @return None
 */
static void nvicVectorTableUpdate(uint32_t offset)
{
    // Set the Vector Table base address offset
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, offset);
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
}

static void clockUpdate()
{
    //try to set up pll loop and run fastest clock
    SystemCoreClockUpdate();
}


void fwInitAll()
{
    //watchdog must be initialized as the first plase for the safety reasons
    nvicVectorTableUpdate(0x5200);
    clockUpdate();
}
