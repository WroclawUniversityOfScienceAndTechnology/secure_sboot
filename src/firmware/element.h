#pragma once

/* Initialize the shared HAL, FW1 vector table, LED and UART command parser. */
void elementInit(void);

/* Confirm startup once, blink PC13 and service AT commands indefinitely. */
void elementRun(void);
