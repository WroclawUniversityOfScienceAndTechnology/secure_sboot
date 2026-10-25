#include "cmd_at.h"
#include "simple_output_builder.h"

/**
 * @brief Force device standby
 * @param at_message - input message
 * @param message_out - output message which shall be set
 * @param message_out_size - output message size which shall be set
 * @return true in case of success, false otherwise
 */
bool cmdAt(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    return buildSimpleOutputMessage(message_out, message_out_size, true);
}
