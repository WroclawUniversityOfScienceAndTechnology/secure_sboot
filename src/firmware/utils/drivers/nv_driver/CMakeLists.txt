

cmake_minimum_required(VERSION 3.14)
set(INTERNAL_NAME NvDriver)

AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} WILDCARD_SRC)

add_library(${INTERNAL_NAME}
    STATIC
        ${WILDCARD_SRC}
)

target_link_libraries(${INTERNAL_NAME}
    PUBLIC
        stmlib
)

target_include_directories(${INTERNAL_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
