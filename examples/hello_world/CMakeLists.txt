cmake_minimum_required(VERSION 3.20)

# Example mod: Hello World
project(hello_world_mod VERSION 1.0.0)

# Create the mod as a shared library
add_library(hello_world_mod SHARED hello_world.cpp)

# Strip the "lib" prefix mingw toolchains add by default so the artifact is
# hello_world_mod.dll (matches the wolf-loader convention).
set_target_properties(hello_world_mod PROPERTIES PREFIX "")

# Include the WOLF API
target_include_directories(hello_world_mod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Set C++ standard
target_compile_features(hello_world_mod PRIVATE cxx_std_17)
