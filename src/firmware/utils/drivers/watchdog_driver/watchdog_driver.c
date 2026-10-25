


#include "watchdog_driver.h"
#include "stm32f10x.h"
#include "stm32f10x_iwdg.h"

#define WATCHDOG_MAX_RELOAD_TIME 0x0FFE // max value 0x0FFF = 4095

bool watchdogInit(void)
{
#ifndef DEBUG
    //independent wdg
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(WATCHDOG_MAX_RELOAD_TIME);
    IWDG_ReloadCounter(); //reload watchdog
    IWDG_Enable();        // Turn on IWDG and LSI
#endif
    return true;
}

void watchdogReset(void)
{
    IWDG_ReloadCounter();
}
