#include <stdlib.h>
#include "cmd_file.h"
#include <string.h>
#include "params_parser.h"
#include "simple_output_builder.h"

static bool buildOpenResponse(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    uint8_t command_ptr = at_message->command_len - AT_SIZE;

    memcpy(message_out, at_message->command + AT_SIZE, command_ptr);
    // TODO this is default descriptor, can be extended
    char* resp = ": 0\n";
    memcpy(message_out + command_ptr, resp, strlen(resp));
    command_ptr += strlen(resp);
    *message_out_size = command_ptr;

    return true;
}

bool cmdFOpen(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    // return file/buffer descriptor
    Params params = parseParams(at_message->data, at_message->data_size);

    // we shall have =<file_name>,(<mode>)
    // mode open new or overwrite existing for write by default
    if (params.size != 2)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    return buildOpenResponse(at_message, message_out, message_out_size);
}

static bool buildWriteResponse(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size,
                               uint16_t write_len)
{
    uint8_t command_ptr = at_message->command_len - AT_SIZE;

    memcpy(message_out, at_message->command + AT_SIZE, command_ptr);

    char buffer[10];
    itoa(write_len, buffer, 10);

    char* resp = ": ";
    memcpy(message_out + command_ptr, resp, strlen(resp));
    command_ptr += strlen(resp);
    memcpy(message_out + command_ptr, buffer, strlen(buffer));
    command_ptr += strlen(buffer);
    message_out[command_ptr] = '\n';

    *message_out_size = command_ptr;

    return true;
}

bool cmdFWrite(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    // return file/buffer descriptor
    Params params = parseParams(at_message->data, at_message->data_size);

    // we shall have =<desctiptor>,<hex data>
    // add to the file/buffer at the end
    if (params.size != 2)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    uint16_t write_len = 0;
    return buildWriteResponse(at_message, message_out, message_out_size, write_len);
}

bool cmdFClose(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    return buildSimpleOutputMessage(message_out, message_out_size, true);
}
