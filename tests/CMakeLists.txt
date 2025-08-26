# Wolf Framework Unit Tests
cmake_minimum_required(VERSION 3.21)

# Find Catch2 from vcpkg
find_package(Catch2 CONFIG REQUIRED)

# Test executable
add_executable(wolf-tests
    test_wolf_api.cpp
)

target_include_directories(wolf-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime
)

target_link_libraries(wolf-tests
    Catch2::Catch2WithMain
)

target_compile_features(wolf-tests PRIVATE cxx_std_23)

# Enable testing
include(CTest)
include(Catch)
catch_discover_tests(wolf-tests)
