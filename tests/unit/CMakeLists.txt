add_executable(wolf-tests
    main.cpp
    support.cpp
    arena_test.cpp
    boundary_test.cpp
    log_test.cpp
    run_queue_test.cpp
    registry_test.cpp
    table_test.cpp
    threading_test.cpp
    tier_test.cpp
    version_test.cpp)
target_link_libraries(wolf-tests PRIVATE doctest wolf_version wolf_core)

add_test(NAME wolf-tests COMMAND wolf-tests)
if(WOLF_TEST_ENV)
    set_tests_properties(wolf-tests PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${WOLF_TEST_ENV}")
endif()
