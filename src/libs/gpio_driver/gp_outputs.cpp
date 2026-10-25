#include "gp_outputs.h"

#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>

uint16_t output_map[1];

void static mapOutputs()
{
    output_map[0] = GPIO_Pin_13;
}

void static initOutputs()
{
    GPIO_InitTypeDef gpio_init_struct;
    // init clocks for GPIO
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio_init_struct.GPIO_Pin = output_map[0];
    gpio_init_struct.GPIO_Speed = GPIO_Speed_10MHz;
    gpio_init_struct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &gpio_init_struct);

    // set all outputs on predefine level
    setOutputState(0, true);
}

void outputsInit()
{
    mapOutputs();
    initOutputs();
}

void setOutputState(uint8_t output_no, bool level)
{
    GPIO_WriteBit(GPIOC, output_map[output_no], static_cast<BitAction>(level));
}
