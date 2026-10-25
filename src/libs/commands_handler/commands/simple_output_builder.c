#include "simple_output_builder.h"
#include <string.h>

bool buildSimpleOutputMessage(uint8_t* message_out, uint16_t* message_out_size, bool status)
{
    // send status if there was some error during processing
    if (!status)
    {
        char* err = "ERROR\n";
        *message_out_size = strlen(err);
        memcpy(message_out, err, *message_out_size);
    }
    else
    {
        char* ok = "OK\n";
        *message_out_size = strlen(ok);
        memcpy(message_out, ok, *message_out_size);
    }

    return true;
}

uint8_t hexDecode(const char* in)
{
    uint16_t hn = in[0] > '9' ? in[0] - 'A' + 10 : in[0] - '0';
    uint16_t ln = in[1] > '9' ? in[1] - 'A' + 10 : in[1] - '0';
    return (hn << 4) | ln;
}
