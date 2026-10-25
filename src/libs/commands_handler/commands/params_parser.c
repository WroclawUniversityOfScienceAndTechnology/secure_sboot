#include "params_parser.h"

Params parseParams(char* data, uint16_t data_size)
{
    Params params = {0};
    if (data_size == 0 || data[0] != '=')
        return params;
    uint16_t start = 1;
    for (uint16_t i = 1; i <= data_size; ++i)
    {
        if (i == data_size || data[i] == ',' || data[i] == '\r' ||
            data[i] == '\n' || data[i] == '\0')
        {
            if (params.size == MAX_PARAMS_SIZE)
            {
                params.size = MAX_PARAMS_SIZE + 1;
                return params;
            }
            params.params[params.size].data = (uint8_t*)data + start;
            params.params[params.size++].len = i - start;
            if (i == data_size || data[i] != ',')
                break;
            start = i + 1;
        }
    }
    return params;
}
