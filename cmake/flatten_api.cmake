# Wolf API Flattening System
# Combines split API headers into a single distributable wolf.hpp
#
# This script processes the individual wolf API headers and combines them
# with the generated version header to create a complete single-header distribution.

cmake_minimum_required(VERSION 3.20)

# Configuration
set(API_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/api")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(VERSION_HEADER "${CMAKE_BINARY_DIR}/include/wolf_version.h")
set(FLAT_OUTPUT_FILE "${CMAKE_BINARY_DIR}/dist/wolf_framework.hpp")

# Auto-discover API headers from both locations
file(GLOB API_HEADER_FILES
    RELATIVE "${API_SOURCE_DIR}"
    "${API_SOURCE_DIR}/wolf_*.hpp"
)

# Add wolf-specific headers from include/ directory (excluding okami/ subdirectory)
file(GLOB WOLF_INCLUDE_FILES
    "${INCLUDE_DIR}/wolf_types.h"
    "${INCLUDE_DIR}/wolf_function_table.h"
)

# Minimal priority list - only for cases where dependency analysis needs help
set(API_HEADER_PRIORITY
    "wolf_types.h"          # Must be first (defines all basic types)
    "wolf_function_table.h" # Must be second (defines function table using types)
    "wolf_core.hpp"         # Must be third (uses function table)
)


function(flatten_wolf_api)
    message(STATUS "Flattening Wolf API into single header...")

    # Find Python executable
    find_program(PYTHON_EXECUTABLE NAMES python python3 py)
    if(NOT PYTHON_EXECUTABLE)
        message(FATAL_ERROR "Python executable not found. Please install Python 3 and ensure it's in your PATH.")
    endif()

    # Ensure output directory exists
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

    # Build arguments for Python script
    set(PYTHON_ARGS
        "--api-dir" "${API_SOURCE_DIR}"
        "--include-dir" "${INCLUDE_DIR}"
        "--output" "${FLAT_OUTPUT_FILE}"
        "--build-type" "${CMAKE_BUILD_TYPE}"
        "--compiler" "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    )

    # Add version header if it exists
    if(EXISTS "${VERSION_HEADER}")
        list(APPEND PYTHON_ARGS "--version-header" "${VERSION_HEADER}")
    endif()

    # Run Python script
    set(PYTHON_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/flatten_headers.py")
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" "${PYTHON_SCRIPT}" ${PYTHON_ARGS}
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERROR
    )

    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "Python header flattening failed: ${ERROR}")
    endif()

    # Display Python script output
    if(OUTPUT)
        message(STATUS "${OUTPUT}")
    endif()

endfunction()

# Add target for flattening (only if it doesn't already exist)
function(add_flatten_target)
    if(NOT TARGET flatten_api)
        add_custom_target(flatten_api
            COMMAND ${CMAKE_COMMAND}
            -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
            -DCMAKE_CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
            -P ${CMAKE_CURRENT_LIST_FILE}
            COMMENT "Flattening Wolf API headers"
            VERBATIM
        )

        # Make it depend on version header generation
        if(TARGET generate_version_header)
            add_dependencies(flatten_api generate_version_header)
        endif()
    endif()
endfunction()

# Allow this script to be called standalone or as part of build
if(CMAKE_SCRIPT_MODE_FILE)
    # Called via cmake -P, perform the flattening
    flatten_wolf_api()
else()
    # Called from CMakeLists.txt - run flattening during configuration AND add target for manual runs
    message(STATUS "Running API flattening during configuration...")
    
    # Make CMake track API source files for reconfiguration
    file(GLOB API_SOURCE_FILES "${API_SOURCE_DIR}/wolf_*.hpp")
    file(GLOB WOLF_INCLUDE_FILES "${INCLUDE_DIR}/wolf_*.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS 
        ${API_SOURCE_FILES} 
        ${WOLF_INCLUDE_FILES}
        "${CMAKE_SOURCE_DIR}/scripts/flatten_headers.py"
    )
    
    flatten_wolf_api()
    add_flatten_target()
endif()
