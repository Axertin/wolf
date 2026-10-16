# The public headers, included as <wolf/wolf.h>.
add_library(wolf_sdk INTERFACE)
target_include_directories(wolf_sdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
