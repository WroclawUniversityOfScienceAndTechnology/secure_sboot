#include "cmd_reset.h"
#include <stm32f1xx_hal.h>
#include "simple_output_builder.h"

bool cmdReset(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    NVIC_SystemReset();
    // if reset wont happen return false
    return buildSimpleOutputMessage(message_out, message_out_size, false);
}
