

#include "calibration.h"
#include "nv_config.h"
#include "string.h"
#include "stdlib.h"
#include "ctype.h"

#define NV_DATA_LEN (sizeof(uint32_t)*(sizeof(ConfigStruct)/4 + 1))

#define CONFIG_MAGIC "PWRMG"
#define SERIAL "Serial"
#define DRIVER_ID "DriverId"
#define BAUDRATE "Baud"

#define DEFAULT_SERIAL "0000000000000000"
#define DEFAULT_DRIVER_ID_MAJOR "C01"
#define DEFAULT_DRIVER_ID_MINOR "000"
#define DEFAULT_DRIVER_TYPE (DEFAULT_DRIVER_ID_MAJOR"."DEFAULT_DRIVER_ID_MINOR)

ConfigStruct conf_struct;

//Returns true if s is a number else false
static bool isNumber(const char *s)
{
	uint16_t i = 0;
	int length = strlen(s);

	for (i = 0; i < length; i++)
		if (isdigit(s[i]) == false)
			return false;

	return true;
}

//Return false is the length is greater than expected
static bool checkLength(const char *s, uint16_t max_accept_length)
{
	int length = strlen(s);
	if (length > max_accept_length)
	{
		return false;
	}
	return true;
}

bool configStrucParamAssign(ConfigStruct *config, char *param)
{
	//use ++pos; to omit '=' char
    if (!memcmp(param, CONFIG_MAGIC, strlen(CONFIG_MAGIC)))
    {
        memcpy(config->conf_magic, param, strlen(CONFIG_MAGIC));
    }
    else if (!memcmp(param, SERIAL, strlen(SERIAL)))
	{
		char *pos = strstr(param, "=");
		pos++;
		if (!(checkLength(pos, sizeof(config->serial)) && isNumber(pos)))
		{
			return false;
		}
		if (sizeof(config->serial) != strlen(pos))
		{
			return false;
		}
		memcpy(config->serial, pos, strlen(pos));
	}
	else if (!memcmp(param, DRIVER_ID, strlen(DRIVER_ID)))
	{
		char *pos = strstr(param, "=");
		pos++;
        if (!(checkLength(pos, sizeof(config->driver_id))))
        {
            return false;
        }
		if (sizeof(config->driver_id) != strlen(pos))
		{
			return false;
		}
		memcpy(config->driver_id, pos, strlen(pos));
	}
	else if (!memcmp(param, BAUDRATE, strlen(BAUDRATE)))
	{
		char *pos = strstr(param, "=");
		char *p_end;
		pos++;
		if (!isNumber(pos))
		{
			return false;
		}
		config->baudrate = strtol(pos, &p_end, 10);
		if (!config->baudrate)
		{
			return false;
		}
		//we have only to typed of detector drivers
		if (config->baudrate != 115200 && config->baudrate != 9600
				&& config->baudrate != 19200 && config->baudrate != 38400
				&& config->baudrate != 57600)
		{
			return false;
		}
	}
	return true;
}

/**
 * @brief  set the driver calibration structure. If there is no valid data in the NV
 *	used default values
 * @retval true is the configuration is prepared, false if default values was used
 */
bool prepareConfigStruct(void)
{
	bool res = false;

	uint32_t *tmp = malloc(NV_DATA_LEN);
	if (!tmp)
	{
		return false;
	}

	res = nvConfigRead(NV_BLOCK1, tmp, NV_DATA_LEN);
	if (res)
	{
		memcpy(&conf_struct, tmp, sizeof(ConfigStruct));
	}

	ConfigStruct cs =
	{ .conf_magic = CONFIG_MAGIC, .serial = DEFAULT_SERIAL, .driver_id =
			DEFAULT_DRIVER_TYPE, .baudrate = DEFAULT_BAUDRATE };

	//check if the configuration is proper , if not set default values
	if (memcmp(conf_struct.conf_magic, cs.conf_magic,
			sizeof(conf_struct.conf_magic)) || !res)
	{
		memcpy(&conf_struct, &cs, sizeof(conf_struct));
	}

	free(tmp);
	return true;
}

/**
 * @brief  pointer to configuration structure. Warning! this function is not protected
 * @retval None
 */
const ConfigStruct* pointerConfigStruct()
{
	return &conf_struct;
}

/**
 * @brief  clone the configuration stucture
 * @param  config - configuration structure to which we want to clone device configuration
 * @return None
 */
void cloneConfigStruct(ConfigStruct *config)
{
	memcpy(config, &conf_struct, sizeof(ConfigStruct));
	memset(&config->conf_magic[0], 0, sizeof(config->conf_magic));
}

/**
 * @brief  check if the magic is correct, if yes update the configuration
 *	update the config structure 
 * @param  config - configuration structure to which we want to update device configuration
 * @return true if update fully succeed, false otherwise
 */
bool updateConfigStruct(ConfigStruct *config)
{
	if (!memcmp(conf_struct.conf_magic, config->conf_magic,
			sizeof(conf_struct.conf_magic)))
	{
		memcpy(&conf_struct, config, sizeof(conf_struct));

		bool res = false;
        uint32_t tmp[NV_DATA_LEN] = { 0 };
		memcpy(tmp, &conf_struct, sizeof(conf_struct));
		res = nvConfigEraseAndProgram(NV_BLOCK1, tmp, NV_DATA_LEN);
		return res;
	}

	return false;
}
