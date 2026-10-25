


#include "health_manager.h"
#include "watchdog_driver.h"
#include "FreeRTOS.h"
#include "task.h"

void vTaskWatchDog(void *pvParameters);

static void runImplementation()
{
	watchdogReset();
}

/**
  * @brief  watchdog initialization. Its run the watch dog preventing 
	* infinite exception loop, etc. in the first place
  * @retval None
  */
bool healtManagerInit(void)
{
	return watchdogInit();
}

/**
  * @brief  Create all health management task (presently only watch dog)
  * @retval None
  */
void healtManagerTaskCreate(void)
{
	xTaskCreate(vTaskWatchDog, "HealtManager", configMINIMAL_STACK_SIZE, NULL, HEALTH_MENAGEMENT_TASK_PRIO, NULL);
}

/**
  * @brief  Task - watchdog reload counter
  * @param  pvParameters - task param (not in use)
  * @retval None
  */
void vTaskWatchDog(void *pvParameters)
{
	while (true)
	{
		runImplementation();
		//reload the watch dog
		vTaskDelay(WATCHDOG_RELOAD_DELAY);
	}
}
