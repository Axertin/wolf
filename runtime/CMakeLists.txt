# The whole runtime is one module, dinput8.dll.
add_library(wolf_runtime SHARED
    proxy/proxy.cpp
    proxy/dinput8.def)
set_target_properties(wolf_runtime PROPERTIES OUTPUT_NAME dinput8 PREFIX "")

# WOLF's own code builds without exceptions, so the compiler rejects throw
# and try here. _HAS_EXCEPTIONS is left alone so the STL is configured the
# same in every file, and /wd4530 silences the warning its headers then give.
if(MSVC)
    target_compile_options(wolf_runtime PRIVATE /EHs-c- /wd4530)
else()
    target_compile_options(wolf_runtime PRIVATE -fno-exceptions)
endif()
target_compile_definitions(wolf_runtime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
