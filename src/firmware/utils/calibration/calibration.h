

#pragma once

#include <stdint.h>
#include <stdbool.h>
#define DEBUG_MODE 

#define COMMAND_MAGIC "AT+PWRMG=" //PWRMG attention for uart data

#define MESSAGE_REC_BUFFER 128
#define MEASURED_CHANNELS_NO 4

#define DEFAULT_BAUDRATE 115200

#define MAGIC_LEN 4
#define SERIAL_LEN 16
#define DRIVERID_LEN 7 // major + minor => 000.000

typedef struct
{
    uint8_t conf_magic[MAGIC_LEN];
    uint8_t serial[SERIAL_LEN];
    uint8_t driver_id[DRIVERID_LEN];
    uint32_t baudrate;
} ConfigStruct;


//prio of the program tasks
// #define HEALTH_MENAGEMENT_TASK_PRIO 1U 
// #define DRIVER_EVENTS_TASK_PRIO 4U
// #define DRIVER_TASK_PRIO 5U

bool prepareConfigStruct(void);
void cloneConfigStruct(ConfigStruct *config);
bool updateConfigStruct(ConfigStruct *config);
bool configStrucParamAssign(ConfigStruct *config_struct, char *param);
const ConfigStruct* pointerConfigStruct(void);
