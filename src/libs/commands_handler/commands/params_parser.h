#pragma once

#ifndef _PARAMS_PARSER_H
#define _PARAMS_PARSER_H

#include <stdint.h>

#define MAX_PARAMS_SIZE 8

typedef struct
{
    uint8_t* data;
    uint16_t len;
} Param;

typedef struct
{
    uint16_t size;
    Param params[MAX_PARAMS_SIZE];
} Params;

Params parseParams(char* data, uint16_t data_size);

#endif
