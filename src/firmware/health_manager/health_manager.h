

#pragma once

#include <stdbool.h>

#define WATCHDOG_RELOAD_DELAY (1000 * portTICK_RATE_MS)
#define HEALTH_MENAGEMENT_TASK_PRIO 1U

bool healtManagerInit(void);
void healtManagerTaskCreate(void);

