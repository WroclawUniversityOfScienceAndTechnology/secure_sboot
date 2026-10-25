cmake_minimum_required(VERSION 3.14)
set(INTERNAL_NAME AtCommandsHandler)

AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} WILDCARD_SRC)
file(GLOB_RECURSE LIBS ${CMAKE_CURRENT_SOURCE_DIR})
AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR}/commands COMMANDS_DIR)

add_library(${INTERNAL_NAME}
    STATIC
        ${WILDCARD_SRC}
        ${COMMANDS_DIR}
)

target_link_libraries(${INTERNAL_NAME}
    PRIVATE
        DevLogger
        stmlib
        NonVolatileItems
        MemoryMap
        FlashDriver
)

target_include_directories(${INTERNAL_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
