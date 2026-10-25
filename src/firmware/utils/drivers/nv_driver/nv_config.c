

#include "nv_config.h"
#include "stm32f10x_flash.h"
#include "stm32f10x.h"

/**
 * @brief  erase flash page
 * @param  block - data block number
 * @retval True if flash erased succesfully
 */
static bool nvConfigFlashErase(NvBlocks block)
{
	uint32_t saddr = BANK1_WRITE_START_ADDR + (block * FLASH_PAGE_SIZE);
	FLASH_Status FLASHStatus = FLASH_COMPLETE;
	//Unlock the Flash Bank1 Program Erase controller 
	FLASH_UnlockBank1();
	//Clear All pending flags 
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	//Erase last FLASH page for 
	FLASHStatus = FLASH_ErasePage(saddr);
	FLASH_LockBank1();

	if (FLASHStatus == FLASH_COMPLETE)
	{
		return true;
	}

	return false;
}

/**
 * @brief  write new data to the flash config page. data validation after flash programming
 *					Note: remember erase the flash before writing
 * @param saddr - start addres for of writing to flash
 * @param data - pointer to data to be stored in flash 
 * @param size  - size of the data (must be lower than 1024)
 * @retval True if succedd
 */
static bool nvConfigFlashFrogram(const uint32_t saddr, const uint32_t* data,
		const uint16_t size)
{
	uint32_t address = saddr;
	int i = 0;

	FLASH_Status FLASHStatus = FLASH_COMPLETE;
	//Unlock the Flash Bank1 Program Erase controller 
	FLASH_UnlockBank1();
	//Clear All pending flags 
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	//if all good try to program the page
	if (FLASHStatus == FLASH_COMPLETE)
	{
		for (i = 0; i < size; i++)
		{
			if (address >= BANK1_WRITE_END_ADDR)
			{
				break;
			}
			FLASHStatus = FLASH_ProgramWord(address, data[i]);

			if (FLASHStatus != FLASH_COMPLETE)
			{
				//if fali to program the flash
				FLASH_LockBank1();
				return false;
			}
			address += sizeof(uint32_t);
		}
	}

	FLASH_LockBank1();

	//Check the correctness of written data 
	/*for(i = 0; i < size; i++)
	 {
	 if(address >= BANK1_WRITE_END_ADDR)
	 {
	 return false;
	 }
	 if(data[i] != (*(__IO uint32_t*) address))
	 {
	 return false;
	 }
	 address += sizeof(uint32_t);
	 }*/

	return true;
}

/**
 * @brief get data from the flash.
 * @param saddr - start address for of writing to flash
 * @param data - pointer to data to be stored in flash 
 * @param size  - size of the data (must be lower than 1024)
 * @retval True if succed
 */
static bool nvConfigFlashRead(uint32_t saddr, uint32_t* data, uint16_t size)
{
	uint32_t address = saddr;
	int i = 0;

	//Check the correctness of written data 
	for (i = 0; i < size; i++)
	{
		if (address >= BANK1_WRITE_END_ADDR)
		{
			return false;
		}
		//read adress from flash
		data[i] = (*(__IO uint32_t*) address);
		address += sizeof(uint32_t);
	}

	return true;
}

/**
 * @brief  write new data to the flash config page. data validation after flash programming
 * @param  block - data block number
 * @param  data - pointer to data to be stored in flash 
 * @param  size  - size of the data (must be lower than 1024)
 * @retval true if flashing suceed
 */
bool nvConfigEraseAndProgram(const NvBlocks block, const uint32_t *data,
		const uint32_t size)
{
	if (block > NV_BLOCK_MAX)
	{
		return false;
	}
	if (!nvConfigFlashErase(BANK1_WRITE_START_ADDR + (block * FLASH_PAGE_SIZE)))
	{
		return false;
	}
	return nvConfigFlashFrogram(
			(BANK1_WRITE_START_ADDR + (block * FLASH_PAGE_SIZE)), data, size);
}

/**
 * @brief  get configuration data from the flash config page.
 * @param  block - data block number
 * @param  data - pointer to data to be stored in flash 
 * @param  size  - size of the data (must be lower than 1024)
 * @retval true if read suceed
 */
bool nvConfigRead(const NvBlocks block, uint32_t* data, uint32_t size)
{
	if (block > NV_BLOCK_MAX)
	{
		return false;
	}
	return nvConfigFlashRead(BANK1_WRITE_START_ADDR + (block * FLASH_PAGE_SIZE),
			data, size);
}
