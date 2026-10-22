# fake_main.dll stands in for the game's main.dll. It imports dinput8.dll from
# the runtime's import library, so loading it pulls in the proxy the way the
# real main.dll does through flower_kernel.dll. wolf-gametests copies it to
# main.dll with the fingerprint written into the PE header.
add_library(fake_main SHARED fake_main.cpp)
set_target_properties(fake_main PROPERTIES PREFIX "")
target_link_libraries(fake_main PRIVATE wolf_runtime)
# The fake reads the symbol table's RVAs, the same header the runtime hooks from.
target_include_directories(fake_main PRIVATE "${PROJECT_SOURCE_DIR}/runtime")

add_executable(wolf-gametests
    game_test.cpp
    harness.cpp
    proxy_test.cpp)
target_link_libraries(wolf-gametests PRIVATE doctest dxguid)
target_include_directories(wolf-gametests PRIVATE "${PROJECT_SOURCE_DIR}/runtime")
# doctest's own SEH handling installs an unhandled-exception filter around every case under MSVC, which would hide whether WOLF's is installed.
target_compile_definitions(wolf-gametests PRIVATE DOCTEST_CONFIG_NO_WINDOWS_SEH)
add_dependencies(wolf-gametests fake_main wolf_runtime)

# The game tests run three times: with -MODDED, as players launch WOLF; without it, where the proxy only forwards; and with -MODDED where dinput8.dll loads
# before main.dll, as it does when the Steam overlay or another injector loads it first. They all write main.dll and logs\ next to the exe, so they never run
# at once.
add_test(NAME wolf-gametests COMMAND wolf-gametests -MODDED -tse=unmodded)
add_test(NAME wolf-gametests-unmodded COMMAND wolf-gametests -tse=modded)
add_test(NAME wolf-gametests-proxy-first COMMAND wolf-gametests -MODDED -ts=modded)
set_tests_properties(wolf-gametests wolf-gametests-unmodded wolf-gametests-proxy-first PROPERTIES RESOURCE_LOCK gamedir)
if(WOLF_TEST_ENV)
    set_tests_properties(wolf-gametests wolf-gametests-unmodded wolf-gametests-proxy-first PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${WOLF_TEST_ENV}")
endif()
set_property(TEST wolf-gametests-proxy-first APPEND PROPERTY ENVIRONMENT "WOLF_GAMETESTS_PROXY_FIRST=1")
