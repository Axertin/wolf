
include(${CMAKE_SOURCE_DIR}/cmake/resource_files.cmake)

execute_process(
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTARGET_NAME=loader
    -DSEARCH_DIR=src/loader
    -P ${CMAKE_SOURCE_DIR}/cmake/generate_sources.cmake
)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

# Configure version resource on Windows
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(WIN32)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/wolf-loader.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/wolf-loader.rc"
            @ONLY
        )
        list(APPEND loader_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/wolf-loader.rc")
    endif()
else()
    message(STATUS "Skipping resource compilation for ${CMAKE_BUILD_TYPE} build")
endif()

add_executable(wolf-loader ${loader_SOURCES})
target_compile_features(wolf-loader PUBLIC ${STD_FEATURE})
target_include_directories(wolf-loader PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CROSS_COMPILING_WINDOWS)
    target_compile_options(wolf-loader PRIVATE -Wa,-mbig-obj -g1)
    target_link_options(wolf-loader PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
        LINKER:--allow-multiple-definition
        LINKER:--enable-stdcall-fixup)
endif()

enable_strict_warnings(wolf-loader)
apply_release_optimizations(wolf-loader)

install(TARGETS wolf-loader RUNTIME DESTINATION .)
