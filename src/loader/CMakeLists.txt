
include(${CMAKE_SOURCE_DIR}/cmake/resource_files.cmake)

execute_process(
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTARGET_NAME=loader
    -DSEARCH_DIR=src/loader
    -P ${CMAKE_SOURCE_DIR}/cmake/generate_sources.cmake
)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_executable(wolf-loader ${loader_SOURCES})
target_compile_features(wolf-loader PUBLIC ${STD_FEATURE})
target_include_directories(wolf-loader PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)

enable_strict_warnings(wolf-loader)
apply_release_optimizations(wolf-loader)

install(TARGETS wolf-loader RUNTIME DESTINATION .)
