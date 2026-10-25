

cmake_minimum_required(VERSION 3.14)
set(INTERNAL_NAME BootloaderInterface)

set(STM32_IMPLEMENTATION "bt_interface_${CHIP_FAMILY}.c")

add_library(${INTERNAL_NAME}
    STATIC
        ${STM32_IMPLEMENTATION}
        swap.c
        boot_selection.c
)

target_link_libraries(${INTERNAL_NAME}
    PRIVATE
        stmlib
        HalPlatform
        MemoryMap
        FlashDriver
        NonVolatileItems
        FirmwareDetails
        UartDriver
        AtCommands
        ledInterface
)

target_include_directories(${INTERNAL_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
