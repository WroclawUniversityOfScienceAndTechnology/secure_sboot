

#[[
CREATE_VERSION - function generate header file Version.h and populate target_include_directories
    to make it visable by target.

    Arguments:
    - TARGET_NAME - Name of the target to add Version.h (binary or library)
    - MAJOR MINOR PATCH - number of version, integer value
#]]
function(CREATE_VERSION TARGET_NAME MAJOR MINOR PATCH)
    string(REPLACE "." "_" HEADER_GUARD "${PROJECT_NAME}")
    string(TOUPPER ${HEADER_GUARD} HEADER_GUARD)

    #configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.h.in" "generated/version.h") #TODO cmake 3.17 requeried
    configure_file("${CMAKE_SOURCE_DIR}/src/gen/version/version.h.in" "generated/${PROJECT_NAME}_version.h")

    target_include_directories(${TARGET_NAME}
    PRIVATE ${PROJECT_BINARY_DIR}
    )
endfunction()
