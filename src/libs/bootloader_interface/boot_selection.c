#include "bt_interface.h"
#include <firmware_details.h>
#include <generated/memory_map.h>
#include <nv_journal.h>

bool prepareFirmwareBoot(NonVolatileItems* items)
{
    NvState state;
    if (!nvStateLoad(&state))
        return false;
    if (state.phase != SWAP_IDLE)
    {
        /* Partial banks are deliberately not validated or selected yet. */
        bootLog("BL SWAP START\r\n");
        char progress[] = "BL SWAP RESUME page=00 phase=0\r\n";
        progress[20] = '0' + state.page / 10;
        progress[21] = '0' + state.page % 10;
        progress[29] = '0' + state.phase;
        bootLog(progress);
        if (!firmwareSwapResume())
            return false;
    }
    for (unsigned selection = 0; selection < 2; ++selection)
    {
        if (!nviLoad(NVITEMS_ADDRESS, items))
            return false;
        /* Validate the bytes in flash here, regardless of whether the image
         * was downloaded by the application or the bootloader. Selection and
         * every new swap must pass this gate before modifying either bank. */
        bool fw1 = firmwareImageCheck(FW1_BANK_ADDRESS);
        bootLog("BL FW2 CHECK INTEGRITY\r\n");
        bool fw2 = firmwareImageCheck(FW2_BANK_ADDRESS);
        bootLog(fw1 ? "BL FW1 VALID\r\n" : "BL FW1 INVALID\r\n");
        bootLog(fw2 ? "BL FW2 VALID\r\n" : "BL FW2 INVALID\r\n");
        const bool return_fw1 = !fw2 && fw1 && isFirmware2Active(items);
        if (!fw2 && isFirmware2Active(items))
        {
            bootLog("BL FW2 REJECTED INTEGRITY\r\n");
            if (fw1)
            {
                setActiveApp(items, FW1);
            }
        }
        if (!fw1 && !fw2)
            bootLog("BL NO VALID APPLICATION\r\n");
        FirmwaresImageStatus status = (FirmwaresImageStatus)((fw1 ? FW1_VALID : NONE_VALID) |
                                                            (fw2 ? FW2_VALID : NONE_VALID));
        selectActiveFirmware(items, status, (const FirmwareDetails*)FW1_BANK_ADDRESS,
                             (const FirmwareDetails*)FW2_BANK_ADDRESS);
        if (!isFirmware2Active(items))
        {
            if (!nviUpdate(NVITEMS_ADDRESS, items))
                return false;
            if (return_fw1)
                bootLog("BL RETURN FW1\r\n");
            return true;
        }
        bootLog("BL SWAP START\r\n");
        if (!firmwareSwapStart(items))
            return false;
    }
    return false;
}
