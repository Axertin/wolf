# fake_main.dll stands in for the game's main.dll. It imports dinput8.dll from
# the runtime's import library, so loading it pulls in the proxy the way the
# real main.dll does through flower_kernel.dll. wolf-gametests copies it to
# main.dll with the fingerprint written into the PE header.
add_library(fake_main SHARED fake_main.c)
set_target_properties(fake_main PROPERTIES PREFIX "")
target_link_libraries(fake_main PRIVATE wolf_runtime)

add_executable(wolf-gametests
    harness.cpp
    proxy_test.cpp)
target_link_libraries(wolf-gametests PRIVATE doctest dxguid)
target_compile_definitions(wolf-gametests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(wolf-gametests fake_main wolf_runtime)

add_test(NAME wolf-gametests COMMAND wolf-gametests)
if(WOLF_TEST_ENV)
    set_tests_properties(wolf-gametests PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${WOLF_TEST_ENV}")
endif()
