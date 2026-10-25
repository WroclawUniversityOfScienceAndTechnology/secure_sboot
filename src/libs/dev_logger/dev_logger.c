#include "dev_logger.h"

#ifdef LOG_ENABLED

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <circularuart.h>

const char* const LOG_TYPE_TO_STRING[] = {"LOG_TYPE_INFO : ", "LOG_TYPE_DEBUG : ", "LOG_TYPE_ERROR : "};

void devLog(LOG_TYPE log_type, const char* format, ...)
{
    // va_list args;
    // char buffer[DEV_LOG_MAX_CHAR_PER_LINE];

    // va_start(args, format);


    // int len = vsnprintf(buffer, DEV_LOG_MAX_CHAR_PER_LINE,format,args);

    // circularUartSend((uint8_t*)LOG_TYPE_TO_STRING[log_type], strlen(LOG_TYPE_TO_STRING[log_type]));
    // circularUartSend((uint8_t*)buffer, len);

    // va_end(args);
}

#endif
