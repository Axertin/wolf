add_executable(wolf-tests
    main.cpp
    platform_test.cpp
    version_test.cpp)
target_link_libraries(wolf-tests PRIVATE doctest wolf_version)

add_test(NAME wolf-tests COMMAND wolf-tests)
if(WOLF_TEST_ENV)
    set_tests_properties(wolf-tests PROPERTIES FIXTURES_REQUIRED wine ENVIRONMENT "${WOLF_TEST_ENV}")
endif()
