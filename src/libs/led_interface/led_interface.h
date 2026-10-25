#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdbool.h>

#define LED_GPIO_PIN GPIO_PIN_13
#define LED_GPIO_PORT GPIOC
#define LED_TOGGLE_TIME_WINDOW 50000

    void ledInit();
    void ledSetState(bool level);
    void ledToggle(bool);
    void ledDeInit();
    void ledRunCyclic();

#ifdef __cplusplus
}
#endif  // __cplusplus
