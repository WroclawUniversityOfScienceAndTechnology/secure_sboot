#include <firmware_details.h>

#include "element.h"

/* The packer fills this section in the downloadable BIN and debug ELF/HEX. */
const FirmwareHeader header __attribute__((section(".fw_header"), used)) = {0};

/**
 * @brief  Main function
 * @return None
 */
int main(void)
{
    elementInit();

    elementRun();
    // keep the infinite loop for security purpose
    while (true)
    {
    }
}
