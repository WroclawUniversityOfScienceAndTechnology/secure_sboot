#include "flash.h"
#include "stm32f1xx_hal.h"

uint32_t getPageSize(void)
{
    return FLASH_PAGE_SIZE;
}

static bool writable(uint32_t address)
{
    return address >= 0x08004000U && address < 0x08010000U;
}

bool flashErasePage(uint32_t address)
{
    if (!writable(address) || address % FLASH_PAGE_SIZE != 0)
        return false;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return false;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = 1;
    uint32_t page_error;
    bool ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
    for (uint32_t i = 0; ok && i < FLASH_PAGE_SIZE / 4; ++i)
        ok = ((volatile uint32_t*)address)[i] == UINT32_MAX;
    HAL_FLASH_Lock();
    return ok;
}

bool flashProgram(uint32_t address, const uint32_t* data, uint32_t size)
{
    if (!writable(address) || data == NULL || address % 4 != 0 || size == 0 ||
        size > (FLASH_PAGE_SIZE - address % FLASH_PAGE_SIZE) / 4)
        return false;
    for (uint32_t i = 0; i < size; ++i)
        if (((volatile uint32_t*)address)[i] != UINT32_MAX)
            return false;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return false;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    bool ok = true;
    for (uint32_t i = 0; ok && i < size; ++i)
    {
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + i * sizeof(uint32_t), data[i]) == HAL_OK;
        if (ok)
            ok = *(volatile uint32_t*)(address + i * sizeof(uint32_t)) == data[i];
    }
    HAL_FLASH_Lock();
    return ok;
}

bool flashEraseAndProgram(uint32_t address, const uint32_t* data, uint32_t words)
{
    if (data == NULL || words == 0 || words > FLASH_PAGE_SIZE / 4)
        return false;
    return flashErasePage(address) && flashProgram(address, data, words);
}

bool flashRead(uint32_t source_address, uint32_t* data, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i)
        data[i] = ((volatile uint32_t*)source_address)[i];
    return true;
}
