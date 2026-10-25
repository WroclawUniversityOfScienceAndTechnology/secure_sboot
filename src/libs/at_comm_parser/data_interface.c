#include "data_interface.h"

#include <circularuart.h>
#include <string.h>

#define TX_PROGRESS_TIMEOUT_MS 1000U

// Reserve the terminator separately so a full-size command still fits.
static uint8_t frame[RESPONSE_BUFFER_SIZE + 2];
static uint8_t uartTxBuf[TX_BUFFERSIZE];
static uint8_t uartRxBuf[RX_BUFFERSIZE];
static uint16_t frame_size;
static bool discarding;
static bool previous_cr;

static void resetParser(void)
{
    frame_size = 0;
    discarding = false;
    previous_cr = false;
}

void dataInterfaceInit(const uint32_t baudrate)
{
    resetParser();
    circularUartInit(baudrate, DEFAULT_PARITY);
    circularUartStartRx(uartRxBuf, RX_BUFFERSIZE_UART_2N);
    circularUartStartTx(uartTxBuf, TX_BUFFERSIZE_UART_2N);
}

void dataInterfaceClearBuffers(bool rx_clear, bool tx_clear)
{
    if (rx_clear)
    {
        circularUartClearRx();
        resetParser();
    }
    if (tx_clear)
        circularUartClearTx();
}

uint16_t dataInterfaceSend(const uint8_t* data, const uint16_t data_length)
{
    uint16_t sent = 0;
    uint32_t last_progress = HAL_GetTick();

    // Called from foreground code with UART and SysTick interrupts enabled.
    // The TX ISR frees space while we wait; queued TX and incoming RX survive.
    while (sent < data_length)
    {
        const uint16_t accepted = circularUartSend(data + sent, data_length - sent);
        if (accepted != 0)
        {
            sent += accepted;
            last_progress = HAL_GetTick();
        }
        else if ((uint32_t)(HAL_GetTick() - last_progress) >= TX_PROGRESS_TIMEOUT_MS)
        {
            break;
        }
    }
    return sent;
}

Result_t dataInterfaceRead(uint8_t* const data, const uint16_t data_length, uint16_t* const message_size)
{
    uint8_t byte;
    *message_size = 0;

    // Consume only through the first frame. Later bytes stay in the RX ring,
    // including when no new UART input arrives before the next poll.
    while (circularUartReceive(&byte, 1) != 0)
    {
        if (discarding)
        {
            if (previous_cr && byte == '\n')
            {
                resetParser();
                return ERROR_T;
            }
            previous_cr = byte == '\r';
            continue;
        }

        // Search for AT without retaining noise; preserve a trailing A.
        if (frame_size < 2)
        {
            if (frame_size == 1 && byte == 'T')
                frame[frame_size++] = byte;
            else
            {
                frame_size = byte == 'A' ? 1 : 0;
                if (frame_size != 0)
                    frame[0] = byte;
            }
            continue;
        }

        frame[frame_size++] = byte;
        if (frame[frame_size - 2] == '\r' && byte == '\n')
        {
            const uint16_t command_size = frame_size - 2;
            resetParser();
            if (command_size > data_length)
                return ERROR_T;
            memcpy(data, frame, command_size);
            *message_size = command_size;
            return SUCCESS_T;
        }

        if (frame_size == sizeof(frame))
        {
            discarding = true;
            previous_cr = byte == '\r';
        }
    }
    return INPROGRESS_T;
}

uint16_t dataSimpleSend(const uint8_t* data, const uint16_t data_length)
{
    return dataInterfaceSend(data, data_length);
}
