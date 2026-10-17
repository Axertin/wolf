# WOLF's own code builds without exceptions, so the compiler rejects throw
# and try here. _HAS_EXCEPTIONS is left alone so the STL is configured the
# same in every file, and /wd4530 silences the warning its headers then give.
add_compile_options("$<IF:$<CXX_COMPILER_ID:MSVC>,/EHs-c-;/wd4530,-fno-exceptions>")

# Everything the runtime does except the dinput8.dll exports lives in wolf_core,
# so the unit tests link the same code the DLL does.
add_library(wolf_core STATIC
    arena/arena.cpp
    arena/tier.cpp
    core/threading.cpp
    frame/boundary.cpp
    frame/run_queue.cpp
    log/log.cpp
    tables/registry.cpp
    tables/table.cpp)
target_include_directories(wolf_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wolf_core PUBLIC wolf_sdk)

# The whole runtime is one module, dinput8.dll.
add_library(wolf_runtime SHARED
    proxy/proxy.cpp
    proxy/dinput8.def)
set_target_properties(wolf_runtime PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_link_libraries(wolf_runtime PRIVATE wolf_core)
