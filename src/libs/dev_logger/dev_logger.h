#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#include <stdint.h>

#include "dev_logger_cfg.h"

#ifdef LOG_ENABLED
#define TRACE_FUNC() devLog(LOG_DEBUG, "File:%s, Function: %s, Line:%d", __FILE__, __FUNCTION__, __LINE__)
#define DEV_LOG devLog

void devLog(LOG_TYPE log_type, const char* format, ...);
#else
#define TRACE_FUNC() ((void)0)
#define DEV_LOG(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif  // __cplusplus
