# Use Cube CMSIS/startup and HAL, without the legacy BSP.
if(NOT STM32_DEVICE STREQUAL "F103C8")
    message(FATAL_ERROR "This flash layout and HAL port require STM32F103C8T6")
endif()
find_package(CMSIS COMPONENTS STM32F103C8 REQUIRED)
find_package(HAL COMPONENTS STM32F1 REQUIRED)
set(BOOTLOADER_VERSION "0.0.1" CACHE STRING "Bootloader version")
option(SBOOT_BUILD_BOOTLOADER "Build the secondary bootloader" ON)
option(SBOOT_BUILD_FIRMWARE "Build the UART bootloader test application" OFF)
set(CHIP_FAMILY stm32f10x)
# HAL and the command parser must fit the original 16 KiB partition.
# Keep Debug symbols, but use size optimization in every profile.
add_compile_options(-Os)
add_compile_definitions(SINGLE_BANK)
add_subdirectory(gen/memory_map)
add_subdirectory(gen/version)

# Retain the internal driver dependency name while replacing SPL with HAL.
add_library(stmlib INTERFACE)
target_include_directories(stmlib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/hal")
target_link_libraries(stmlib INTERFACE
    CMSIS::STM32::F103xB HAL::STM32::F1::CORTEX HAL::STM32::F1::RCC HAL::STM32::F1::RCCEx
    HAL::STM32::F1::GPIO HAL::STM32::F1::FLASH HAL::STM32::F1::FLASHEx HAL::STM32::F1::UART HAL::STM32::F1::DMA
    STM32::Nano STM32::NoSys)
add_library(HalPlatform STATIC hal/platform.c)
target_link_libraries(HalPlatform PUBLIC stmlib)
foreach(lib non_volatile_items validity bootloader_interface led_interface
            uart_driver flash_driver dev_logger firmware_details at_comm_parser commands_handler)
    add_subdirectory(libs/${lib})
endforeach()
if(SBOOT_BUILD_BOOTLOADER)
    add_subdirectory(bootloader)
endif()
if(SBOOT_BUILD_FIRMWARE)
    add_subdirectory(firmware)
endif()
