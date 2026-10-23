# The smoke mod is copied by hand into a real game's mods directory. It lands in bin/smoke-mods/smoke/, outside bin/mods/, so the game tests never load it.

set(WOLF_MODS_DIR "${CMAKE_BINARY_DIR}/bin/smoke-mods")
wolf_add_mod(smoke SOURCES smoke.c)
