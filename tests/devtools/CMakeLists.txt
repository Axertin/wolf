# DevTools unit tests
cmake_minimum_required(VERSION 3.21)

find_package(Catch2 CONFIG REQUIRED)

add_executable(devtools-tests
    test_state_bit_decoder.cpp
    test_event_buffer.cpp
    test_watches.cpp
)

target_include_directories(devtools-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/api
    ${CMAKE_SOURCE_DIR}/src/devtools
)

# Build the pure-logic .cpp directly into the test executable so we don't have
# to ship a separate static lib for the few testable pieces.
target_sources(devtools-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devtools/runtime_tracer/state_bit_decoder.cpp
)

target_compile_features(devtools-tests PRIVATE cxx_std_23)
target_link_libraries(devtools-tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(devtools-tests)
