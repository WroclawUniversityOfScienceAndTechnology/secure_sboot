project(bootloader VERSION ${BOOTLOADER_VERSION} LANGUAGES C ASM)
add_executable(bootloader main.c ../hal/interrupts.c)
set_target_properties(bootloader PROPERTIES SUFFIX ".elf")
target_link_libraries(bootloader PRIVATE stmlib MemoryMap NonVolatileItems
    Validity BootloaderInterface DevLogger FirmwareDetails)
CREATE_VERSION(bootloader ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
GEN_LINKER_SCRIPT("BOOTLOADER" ${BOOTLOADER_SIZE} ${NV_SIZE} ${FW_SIZE})
stm32_add_linker_script(bootloader PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LinkerScript.ld")
target_link_options(bootloader PRIVATE "-Wl,-Map=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bootloader.map")
stm32_print_size_of_target(bootloader)
foreach(format bin hex)
    if(format STREQUAL "bin")
        set(objcopy_format binary)
    else()
        set(objcopy_format ihex)
    endif()
    add_custom_command(TARGET bootloader POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ${objcopy_format} $<TARGET_FILE:bootloader>
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bootloader.${format}"
        BYPRODUCTS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bootloader.${format}"
        VERBATIM)
endforeach()
