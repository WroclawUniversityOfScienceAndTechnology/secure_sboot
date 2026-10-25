#include <stm32f1xx_hal.h>
#include "led_interface.h"

static volatile bool led_is_togle_enabled;

void ledInit()
{
    GPIO_InitTypeDef gpio_struct = {0};
    // init clocks for GPIO
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // set all outputs on predefine level
    ledSetState(true);

    /*Configure GPIO pin  for LED */

    gpio_struct.Pin = LED_GPIO_PIN;

    gpio_struct.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_struct.Mode = GPIO_MODE_OUTPUT_PP;

    HAL_GPIO_Init(LED_GPIO_PORT, &gpio_struct);
}

void ledSetState(bool level)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool ledGetState()
{
    return (bool)(LED_GPIO_PORT->ODR & LED_GPIO_PIN);
}

void ledToggle(bool state)
{
    led_is_togle_enabled = state;
}

/*This method is beging called from the cyclicly to togle LED */
void ledRunCyclic()
{
    static uint32_t ticks = 0;

    if (led_is_togle_enabled)
    {
        if (ticks > LED_TOGGLE_TIME_WINDOW)
        {
            ticks = 0;

            ledSetState(!ledGetState());
        }

        ticks++;
    }
}

void ledDeInit()
{
    ledToggle(false);

    // init clocks for GPIO
    HAL_GPIO_DeInit(LED_GPIO_PORT, LED_GPIO_PIN);
    __HAL_RCC_GPIOC_CLK_DISABLE();
}
