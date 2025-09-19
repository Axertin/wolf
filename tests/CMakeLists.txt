# Wolf Framework Unit Tests
cmake_minimum_required(VERSION 3.21)

# Find Catch2 from vcpkg
find_package(Catch2 CONFIG REQUIRED)

# Create testable core library
add_library(wolf-core-testable STATIC
    ${CMAKE_SOURCE_DIR}/src/runtime/core/logging.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/core/resource_system.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/core/console_system.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/core/mod_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/core/bitfield_monitoring.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/core/shop_system.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/utilities/shop_registry.cpp
)

target_include_directories(wolf-core-testable PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/core
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/runtime/utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_compile_features(wolf-core-testable PRIVATE cxx_std_23)

# Define testing mode to mock ImGui dependencies  
target_compile_definitions(wolf-core-testable PRIVATE
    WOLF_TESTING_MODE=1
    IMGUI_VERSION_NUM=19000
)

# High-level API tests
add_executable(wolf-tests
    test_wolf_api.cpp
)

target_include_directories(wolf-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/api
)

target_link_libraries(wolf-tests
    Catch2::Catch2WithMain
)

target_compile_features(wolf-tests PRIVATE cxx_std_23)

# Core unit tests
add_executable(wolf-core-tests
    core/test_mocks.cpp
    core/test_logging.cpp
    core/test_console_system.cpp
    core/test_mod_lifecycle.cpp
    core/test_shop_registry.cpp
    core/test_shop_system.cpp
)

target_include_directories(wolf-core-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/runtime/core
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/runtime/utilities
)

target_link_libraries(wolf-core-tests
    wolf-core-testable
    Catch2::Catch2WithMain
)

target_compile_features(wolf-core-tests PRIVATE cxx_std_23)

# Enable testing
include(CTest)

# Include Catch2's test discovery
include(Catch)
catch_discover_tests(wolf-tests)
catch_discover_tests(wolf-core-tests)
