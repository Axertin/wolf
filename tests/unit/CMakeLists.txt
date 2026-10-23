add_executable(wolf-tests
    main.cpp
    support.cpp
    api_test.cpp
    binding_test.cpp
    arena_test.cpp
    boot_test.cpp
    boundary_test.cpp
    config_test.cpp
    crash_test.cpp
    file_log_test.cpp
    hooks_race_test.cpp
    hooks_test.cpp
    log_test.cpp
    memory_test.cpp
    mods_test.cpp
    registry_test.cpp
    resolve_test.cpp
    run_queue_test.cpp
    sites_test.cpp
    table_test.cpp
    threading_test.cpp
    lifetime_test.cpp
    version_test.cpp)
target_link_libraries(wolf-tests PRIVATE doctest wolf_version wolf_core)
# The exe exports two functions under flower_kernel mangled names, so symbol resolution and hook by name can be tested against real exports. CMake applies
# a .def source to DLLs only, so it goes to the linker directly.
set(WOLF_TESTS_DEF "${CMAKE_CURRENT_SOURCE_DIR}/wolf-tests.def")
target_link_options(wolf-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/DEF:${WOLF_TESTS_DEF},${WOLF_TESTS_DEF}>")
set_property(TARGET wolf-tests APPEND PROPERTY LINK_DEPENDS "${WOLF_TESTS_DEF}")

add_test(NAME wolf-tests COMMAND wolf-tests)
if(WOLF_TEST_ENV)
    set_tests_properties(wolf-tests PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${WOLF_TEST_ENV}")
endif()

# A process that crashes on request, for the crash reporter's tests. It is test code, so it builds with exceptions like the test exe.
add_executable(wolf-crashchild crash_child.cpp)
target_link_libraries(wolf-crashchild PRIVATE wolf_core)
target_compile_options(wolf-crashchild PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
add_dependencies(wolf-tests wolf-crashchild)

# A C++ mod whose init lets an exception escape a noexcept function. It lands in bin/, not bin/mods/, so only the crash child loads it.
add_library(wolf-terminate-mod SHARED terminate_mod.cpp)
set_target_properties(wolf-terminate-mod PROPERTIES PREFIX "")
target_link_libraries(wolf-terminate-mod PRIVATE wolf_sdk)
target_compile_options(wolf-terminate-mod PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
add_dependencies(wolf-tests wolf-terminate-mod)

# The binding with exceptions off, compiled and never linked, so its exceptions-off paths keep building.
add_library(wolf-binding-noexcept OBJECT binding_noexcept.cpp)
target_link_libraries(wolf-binding-noexcept PRIVATE wolf_sdk)
target_compile_options(wolf-binding-noexcept PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/EHs-c-;/D_HAS_EXCEPTIONS=0,-fno-exceptions>")
