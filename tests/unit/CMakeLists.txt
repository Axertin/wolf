add_executable(wolf-tests
    main.cpp
    support.cpp
    arena_test.cpp
    boot_test.cpp
    boundary_test.cpp
    crash_test.cpp
    file_log_test.cpp
    hooks_race_test.cpp
    hooks_test.cpp
    log_test.cpp
    registry_test.cpp
    resolve_test.cpp
    run_queue_test.cpp
    sites_test.cpp
    table_test.cpp
    threading_test.cpp
    lifetime_test.cpp
    version_test.cpp)
target_link_libraries(wolf-tests PRIVATE doctest wolf_version wolf_core)
# The exe exports one function under a flower_kernel mangled name, so symbol resolution can be tested against a real export. CMake applies a .def source to
# DLLs only, so it goes to the linker directly.
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
