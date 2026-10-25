set(FIRMWARE_VERSION "0.0.2" CACHE STRING "Test firmware version (three bytes)")
option(FIRMWARE_CONFIRM_BOOT "Confirm startup in NV after one second" ON)
if(NOT FIRMWARE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "FIRMWARE_VERSION must be major.minor.patch")
endif()
string(REPLACE "." ";" version_parts "${FIRMWARE_VERSION}")
foreach(part IN LISTS version_parts)
    if(part GREATER 255)
        message(FATAL_ERROR "Each FIRMWARE_VERSION component must fit in one byte")
    endif()
endforeach()
project(firmware VERSION ${FIRMWARE_VERSION} LANGUAGES C ASM)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Git QUIET)
set(firmware_git_sha "0000000000000000")
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE git_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE git_result)
    if(git_result EQUAL 0)
        string(SUBSTRING "${git_head}" 0 16 firmware_git_sha)
    endif()
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/firmware-git-sha.txt"
    CONTENT "${firmware_git_sha}\n")

# Keep intermediate images out of pwrforge's public bin directory.
add_executable(firmware_link EXCLUDE_FROM_ALL main.c element.c ../hal/interrupts.c)
set_target_properties(firmware_link PROPERTIES OUTPUT_NAME firmware-unpacked
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(firmware_link PRIVATE HalPlatform MemoryMap FirmwareDetails
    NonVolatileItems AtCommands UartDriver ledInterface)
target_compile_definitions(firmware_link PRIVATE
    FIRMWARE_CONFIRM_BOOT=$<BOOL:${FIRMWARE_CONFIRM_BOOT}>)
CREATE_VERSION(firmware_link ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
GEN_FW_LINKER_SCRIPT(FW1 ${BOOTLOADER_SIZE} ${NV_SIZE} ${FW_SIZE})
stm32_add_linker_script(firmware_link PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LinkerScript.ld")
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
target_link_options(firmware_link PRIVATE "-Wl,-Map=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/firmware.map"
    "-Wl,--nmagic") # Keep ELF load segments inside FW1, including ELF headers.
stm32_print_size_of_target(firmware_link)

set(packer "${CMAKE_SOURCE_DIR}/tools/pack_firmware.py")
set(output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/firmware")
add_custom_command(
    OUTPUT "${output}.bin" "${output}.elf" "${output}.hex"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware_link> firmware-unpacked.bin
    COMMAND ${Python3_EXECUTABLE} "${packer}" firmware-unpacked.bin "${output}.bin"
        --reserved-header --version "${PROJECT_VERSION}"
        --git-sha "${firmware_git_sha}"
        --header-output firmware-header.bin
    COMMAND ${CMAKE_OBJCOPY} --update-section .fw_header=firmware-header.bin
        $<TARGET_FILE:firmware_link> "${output}.elf"
    COMMAND ${CMAKE_OBJCOPY} -O ihex "${output}.elf" "${output}.hex"
    DEPENDS firmware_link "${packer}" "${CMAKE_SOURCE_DIR}/tools/firmware_header.py"
        "${CMAKE_CURRENT_BINARY_DIR}/firmware-git-sha.txt"
    BYPRODUCTS firmware-unpacked.bin firmware-header.bin
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Packaging firmware header, checksum and matching BIN/ELF/HEX"
    VERBATIM)
add_custom_target(firmware ALL DEPENDS "${output}.bin" "${output}.elf" "${output}.hex")
