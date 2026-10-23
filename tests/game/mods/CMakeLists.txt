# The test mods wolf-gametests loads from bin/mods/. Every directory and file written here is one the game tests read.

function(test_mod_file path content)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${path}" CONTENT "${content}")
endfunction()

test_mod_file(chain_a/mod.ini "priority = 1\n")
wolf_add_mod(chain_a SOURCES chain.c FILES "${CMAKE_CURRENT_BINARY_DIR}/chain_a/mod.ini")
target_compile_definitions(chain_a PRIVATE "CHAIN_TAG='A'" [[CHAIN_ID="chain_a"]])

test_mod_file(chain_b/mod.ini "priority = 2\n")
wolf_add_mod(chain_b SOURCES chain.c FILES "${CMAKE_CURRENT_BINARY_DIR}/chain_b/mod.ini")
target_compile_definitions(chain_b PRIVATE "CHAIN_TAG='B'" [[CHAIN_ID="chain_b"]])

test_mod_file(probe/mod.ini "priority = 5\n")
test_mod_file(probe/config.ini "greeting = hi\n")
wolf_add_mod(probe SOURCES probe.c FILES "${CMAKE_CURRENT_BINARY_DIR}/probe/mod.ini" "${CMAKE_CURRENT_BINARY_DIR}/probe/config.ini")

wolf_add_mod(refused SOURCES refused.c)

# deps imports deps_helper.dll, which lands in the mod's own directory.
add_library(deps_helper SHARED helper.c)
set_target_properties(deps_helper PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${WOLF_MODS_DIR}/deps")
wolf_add_mod(deps SOURCES deps.c)
target_link_libraries(deps PRIVATE deps_helper)

wolf_add_mod(noexport SOURCES helper.c)

wolf_add_mod(throwing SOURCES throwing.cpp)

file(WRITE "${WOLF_MODS_DIR}/badbin/badbin.dll" "not a dll")
file(WRITE "${WOLF_MODS_DIR}/assets/data_pc/readme.txt" "an asset-only mod\n")
file(WRITE "${WOLF_MODS_DIR}/stray.txt" "a file directly under mods/\n")

add_dependencies(wolf-gametests chain_a chain_b probe refused deps noexport throwing)
