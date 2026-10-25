#include <string.h>
#include "commands/simple_output_builder.h"
#include "command_handler.h"
#include "commands/at_message.h"
// driver command handlers
#include "commands/cmd_at.h"
#include "commands/cmd_dfu.h"
#include "commands/cmd_reset.h"
#include "commands/cmd_reset_active_app.h"
// file/buffer commands
#include "commands/cmd_file.h"

typedef bool (*commandHandler_t)(const AtMessage *, uint8_t *, uint16_t *);

typedef struct
{
    const char *command;
    commandHandler_t commandHandler;
} commandConfig_t;

static commandConfig_t _config[] = {
    {"AT", cmdAt},
    {"AT+DFUOP", cmdDfuOpen},
    {"AT+DFUWR", cmdDfuWrite},
    {"AT+DFUCL", cmdDfuClose},
    {"AT+RESET", cmdReset},
    {"AT+RAA", cmdResetActiveApp},
    // file/buffer handling
    {"AT+FOPEN", cmdFOpen},
    {"AT+FCLOSE", cmdFClose},
    {"AT+FWRITE", cmdFWrite},
};

// amount of the commands which can be proceed
static const int16_t _config_size = sizeof(_config) / sizeof(_config[0]);

static uint16_t getCommandSize(const uint8_t* message, uint16_t size)
{
    uint16_t i = 0;
    while (i < size && message[i] != '=' && message[i] != '?' &&
           message[i] != '\r' && message[i] != '\n' && message[i] != '\0')
        ++i;
    return i;
}

/**
 * @brief Generic network commands handler
 * @param message_in
 * @param message_size_in
 * @param message_out
 * @param message_out_size
 * @return true in case of success, false otherwise
 */
bool commandHandler(const uint8_t *message_in, const uint16_t message_size_in,
        uint8_t *message_out, uint16_t *message_out_size)
{
    int16_t i = 0;
    AtMessage at_message;
    memset(at_message.command, 0, sizeof(at_message.command));
    uint16_t command_len = getCommandSize(message_in, message_size_in);
    if (command_len >= sizeof(at_message.command))
        return buildSimpleOutputMessage(message_out, message_out_size, false);
    at_message.command_len = command_len;
    memcpy(at_message.command, message_in, at_message.command_len);

    at_message.data_size = message_size_in - at_message.command_len;
    at_message.data = (char*)message_in + at_message.command_len;

    //used the appropriate function depending on the command id
    for (i = 0; i < _config_size; i++)
    {
        if (!strcmp(at_message.command, _config[i].command))
        {
            if (_config[i].commandHandler)
            {
                return _config[i].commandHandler(&at_message, message_out, message_out_size);
            }
        }
    }
    return buildSimpleOutputMessage(message_out, message_out_size, false);
}

