#include <string.h>


#include "at.h"
#include "data_interface.h"
#include <command_handler.h>

static uint8_t buffer[RESPONSE_BUFFER_SIZE];

void atInitUartBuffers()
{
    dataInterfaceInit(BAUDRATE);
}

static void receiveData()
{
    uint16_t message_size;
    Result_t interface_res = dataInterfaceRead(buffer, sizeof(buffer), &message_size);

    if(interface_res == SUCCESS_T)
    {
        uint16_t payload_out_size = 0;
        commandHandler(buffer, message_size, buffer, &payload_out_size);
        dataInterfaceSend(buffer, payload_out_size);
        memset(buffer, 0, sizeof(buffer));
    }
    else if (interface_res == ERROR_T)
    {
        memset(buffer, 0, sizeof(buffer));
    }
}

void atPoolUartBuffer()
{
    receiveData();
}
