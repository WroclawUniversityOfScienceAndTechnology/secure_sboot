#include "element.h"
#include <at.h>
#include <circularuart.h>
#include <generated/firmware_version.h>
#include <generated/memory_map.h>
#include <led_interface.h>
#include <non_volatile_items.h>
#include <platform.h>
#include <stm32f1xx_hal.h>
#include <string.h>

static void sendText(const char* text)
{
    circularUartSend((const uint8_t*)text, strlen(text));
}

void elementInit(void)
{
    platformInit(FW1_APP_ADDRESS);
    ledInit();
    atInitUartBuffers();
    sendText("FW ");
    sendText(getVersion());
    sendText(" running\r\n");
}

void elementRun(void)
{
    const uint32_t started = HAL_GetTick();
    uint32_t last_led = started;
    bool startup_checked = false;
    bool led_state = true;
    while (true)
    {
        const uint32_t now = HAL_GetTick();
        if (!startup_checked && (uint32_t)(now - started) >= 1000U)
        {
            startup_checked = true;
#if FIRMWARE_CONFIRM_BOOT
            NonVolatileItems nv_items;
            const bool loaded = nviLoad(NVITEMS_ADDRESS, &nv_items);
            nv_items.app_valid = 1;
            nv_items.boot_count = 0;
            sendText(loaded && nviUpdate(NVITEMS_ADDRESS, &nv_items) ?
                     "FW confirmed\r\n" : "FW confirmation failed\r\n");
#else
            sendText("FW confirmation disabled\r\n");
#endif
        }
        if ((uint32_t)(now - last_led) >= 500U)
        {
            last_led = now;
            led_state = !led_state;
            ledSetState(led_state);
        }
        /* Confirm only once before accepting changes to NV through AT+RAA. */
        if (startup_checked)
            atPoolUartBuffer();
    }
}
