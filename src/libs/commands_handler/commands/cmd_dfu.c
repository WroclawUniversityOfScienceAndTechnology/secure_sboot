#include <string.h>
#include "cmd_dfu.h"
#include <flash.h>
#include <generated/memory_map.h>
#include <nv_journal.h>
#include <stdlib.h>
#include "params_parser.h"
#include "simple_output_builder.h"

#define DEFAULT_BANK 2
#define PAGE_NUM_PARAM 0
#define DATA_SIZE_PARAM 0
#define BANK_NUM_PARAM 0
#define DATA_PARAM 1

static bool numberParam(Param param, uint16_t* value)
{
    uint32_t result = 0;
    if (param.len == 0)
        return false;
    for (uint16_t i = 0; i < param.len; ++i)
    {
        if (param.data[i] < '0' || param.data[i] > '9')
            return false;
        result = result * 10 + param.data[i] - '0';
        if (result > UINT16_MAX)
            return false;
    }
    *value = result;
    return true;
}

static uint8_t* data_buff = NULL;
static uint16_t data_buff_size = 0;
static uint8_t bank_number = 2;

static uint32_t getBankAddr()
{
    return bank_number == 1 ? FW1_BANK_ADDRESS : FW2_BANK_ADDRESS;
}

static void clearBuffer()
{
    memset(data_buff, 0xFF, getPageSize());
    data_buff_size = 0;
}

static bool flashBuffer(uint16_t page_num)
{
    if (data_buff == NULL || page_num >= FW_SIZE / getPageSize())
        return false;
    if (data_buff_size == 0)
    {
        return true;
    }
    const uint16_t page_size = getPageSize();
    const uint32_t page_size_in_words = page_size / 4;

    const uint16_t offset = page_num * page_size;
    const uint32_t bank_addr = getBankAddr();
    const bool flash_result = flashEraseAndProgram(bank_addr + offset, (uint32_t*)(data_buff), page_size_in_words);

    clearBuffer();

    return flash_result;
}

/**
 * @brief Force device standby
 * @param at_message - input message
 * @param message_out - output message which shall be set
 * @param message_out_size - output message size which shall be set
 * @return true in case of success, false otherwise
 */
bool cmdDfuOpen(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    if (!nvUpdateAllowed())
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    if (data_buff != NULL)
    {
        free(data_buff);
        data_buff = NULL;
    }

    Params params = parseParams(at_message->data, at_message->data_size);

    uint16_t bank_num_to_set = DEFAULT_BANK;
    if (params.size == 1)
    {
        if (!numberParam(params.params[BANK_NUM_PARAM], &bank_num_to_set))
            return buildSimpleOutputMessage(message_out, message_out_size, false);
        if (bank_num_to_set != 1 && bank_num_to_set != 2)
        {
            return buildSimpleOutputMessage(message_out, message_out_size, false);
        }
    }
    if (params.size > 1)
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    bank_number = bank_num_to_set;

    const uint16_t page_size = getPageSize();
    data_buff = malloc(page_size);
    data_buff_size = 0;

    if (data_buff == NULL)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    clearBuffer();

    return buildSimpleOutputMessage(message_out, message_out_size, true);
}

bool cmdDfuWrite(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    if (!nvUpdateAllowed())
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    Params params = parseParams(at_message->data, at_message->data_size);

    if (data_buff == NULL || (params.size != 1 && params.size != 2))
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    if (params.size == 2)
    {
        uint16_t data_size;
        if (!numberParam(params.params[DATA_SIZE_PARAM], &data_size))
            return buildSimpleOutputMessage(message_out, message_out_size, false);
        char* data = (char*)params.params[DATA_PARAM].data;
        if (data_size != params.params[DATA_PARAM].len || data_size % 2 != 0 ||
            data_size / 2 > getPageSize() - data_buff_size)
        {
            return buildSimpleOutputMessage(message_out, message_out_size, false);
        }

        for (uint16_t i = 0; i < data_size; ++i)
            if (!((data[i] >= '0' && data[i] <= '9') ||
                  (data[i] >= 'A' && data[i] <= 'F')))
                return buildSimpleOutputMessage(message_out, message_out_size, false);

        for (uint16_t i = 0; i < data_size; i += 2)
        {
            data_buff[data_buff_size] = hexDecode(data + i);
            ++data_buff_size;
        }

        return buildSimpleOutputMessage(message_out, message_out_size, true);
    }
    else if (params.size == 1)
    {
        uint16_t page_num;
        if (!numberParam(params.params[PAGE_NUM_PARAM], &page_num))
            return buildSimpleOutputMessage(message_out, message_out_size, false);
        const bool flash_result = flashBuffer(page_num);

        return buildSimpleOutputMessage(message_out, message_out_size, flash_result);
    }
    return buildSimpleOutputMessage(message_out, message_out_size, false);
}

bool cmdDfuClose(const AtMessage* at_message, uint8_t* message_out, uint16_t* message_out_size)
{
    if (!nvUpdateAllowed())
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    Params params = parseParams(at_message->data, at_message->data_size);

    if (params.size == 0)
    {
        if (data_buff != NULL)
        {
            free(data_buff);
            data_buff = NULL;
        }
        return buildSimpleOutputMessage(message_out, message_out_size, true);
    }

    if (data_buff == NULL || params.size != 1)
    {
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    }

    uint16_t page_num;
    if (!numberParam(params.params[PAGE_NUM_PARAM], &page_num))
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    const bool flash_result = flashBuffer(page_num);

    free(data_buff);
    data_buff = NULL;

    return buildSimpleOutputMessage(message_out, message_out_size, flash_result);
}
