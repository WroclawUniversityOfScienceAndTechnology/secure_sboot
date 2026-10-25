#include <stdlib.h>
#include <generated/memory_map.h>
#include <non_volatile_items.h>
#include "cmd_at.h"
#include "params_parser.h"
#include "simple_output_builder.h"

#define RAA_MODE_PARAM 0

bool cmdResetActiveApp(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    Params params = parseParams(at_message->data, at_message->data_size);
    NonVolatileItems nv_items;
    if (!nviLoad(NVITEMS_ADDRESS, &nv_items))
        return buildSimpleOutputMessage(message_out, message_out_size, false);

    if (params.size > 1)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    if (params.size == 0)
    {
        nv_items.active_app = 1;
    }

    if (params.size == 1)
    {
        nv_items.active_app = atoi(params.params[RAA_MODE_PARAM].data);
    }

    if (nv_items.active_app > 11)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    nv_items.app_valid = 0;
    nv_items.boot_count = 0;
    const bool result = nviUpdate(NVITEMS_ADDRESS, &nv_items);

    return buildSimpleOutputMessage(message_out, message_out_size, result);
}
