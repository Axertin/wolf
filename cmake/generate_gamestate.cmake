# generate_gamestate.cmake - Generate C++ headers from YAML game state definitions

function(generate_gamestate_headers)
    set(SCRIPT_PATH "${CMAKE_SOURCE_DIR}/scripts/generate_gamestate_headers.py")
    set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/devtools/game-data")
    set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/include/okami/gamestate")
    
    # Check if Python 3 is available
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_FOUND)
        message(WARNING "Python 3 not found. Skipping gamestate header generation.")
        return()
    endif()
    
    # Check if PyYAML is available by trying to import it
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import yaml"
        RESULT_VARIABLE YAML_CHECK_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    
    if(NOT YAML_CHECK_RESULT EQUAL 0)
        message(WARNING "PyYAML not available. Skipping gamestate header generation. Install with: pip install PyYAML")
        return()
    endif()
    
    # Check if source files exist
    if(NOT EXISTS "${SOURCE_DIR}/global.yml")
        message(WARNING "Game data files not found at ${SOURCE_DIR}. Skipping gamestate header generation.")
        return()
    endif()
    
    # Always generate headers and let CMake handle dependency tracking
    message(STATUS "Generating gamestate headers from YAML files...")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${SCRIPT_PATH}" 
            --source-dir "${SOURCE_DIR}"
            --output-dir "${OUTPUT_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE GENERATION_RESULT
        OUTPUT_VARIABLE GENERATION_OUTPUT
        ERROR_VARIABLE GENERATION_ERROR
    )
    
    if(GENERATION_RESULT EQUAL 0)
        message(STATUS "Gamestate headers generated successfully")
        if(GENERATION_OUTPUT)
            message(STATUS "${GENERATION_OUTPUT}")
        endif()
    else()
        message(WARNING "Failed to generate gamestate headers: ${GENERATION_ERROR}")
    endif()
endfunction()