#pragma once

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

/*Disable this for disabling debug*/
#define LOG_ENABLED

#define DEV_LOG_MAX_CHAR_PER_LINE 100U

typedef enum LOG_TYPE
{
    LOG_TYPE_INFO = 0,
    LOG_TYPE_DEBUG = 1,
    LOG_TYPE_ERROR = 2,
} LOG_TYPE;

#ifdef __cplusplus
}
#endif  // __cplusplus
