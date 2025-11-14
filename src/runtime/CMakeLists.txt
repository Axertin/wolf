execute_process(
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTARGET_NAME=runtime
    -DSEARCH_DIR=src/runtime
    -P ${CMAKE_SOURCE_DIR}/cmake/generate_sources.cmake
)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_library(wolf-runtime SHARED ${runtime_SOURCES} dinput8.def)
target_compile_features(wolf-runtime PUBLIC ${STD_FEATURE})
target_include_directories(wolf-runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(wolf-runtime PUBLIC minhook::minhook imgui yaml-cpp::yaml-cpp)

# Add D3D11 libraries for ImGui backend and DirectInput for input hooks
target_link_libraries(wolf-runtime PRIVATE d3d11 dxgi dinput8 dxguid)

# Set output name to dinput8.dll for DLL proxy injection
set_target_properties(wolf-runtime PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CROSS_COMPILING_WINDOWS)
    target_link_libraries(wolf-runtime PRIVATE dwmapi d3dcompiler)
    target_compile_options(wolf-runtime PRIVATE -Wa,-mbig-obj -g1)
    target_link_options(wolf-runtime PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
        LINKER:--allow-multiple-definition
        LINKER:--enable-stdcall-fixup)
endif()

enable_strict_warnings(wolf-runtime)
apply_release_optimizations(wolf-runtime)

install(TARGETS wolf-runtime RUNTIME DESTINATION .)
