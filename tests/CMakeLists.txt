# Under a Linux cross build every test exe runs through Wine. One wineserver, started by a CTest fixture, serves all of them, in a prefix of its own
# under the build directory.
if(CMAKE_CROSSCOMPILING AND CMAKE_HOST_UNIX)
    find_program(WINE_EXECUTABLE NAMES wine wine64 REQUIRED)
    find_program(WINESERVER_EXECUTABLE NAMES wineserver wineserver64 REQUIRED
        PATHS /usr/lib/wine /usr/lib/x86_64-linux-gnu/wine /usr/lib64/wine
              /opt/wine-stable/bin /opt/wine-devel/bin /opt/wine-staging/bin)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${WINE_EXECUTABLE}")

    set(WOLF_WINEPREFIX "${CMAKE_BINARY_DIR}/wineprefix")
    file(MAKE_DIRECTORY "${WOLF_WINEPREFIX}")
    # dinput8=n,b makes Wine load our dinput8.dll over its builtin, as Proton
    # needs too. mscoree and mshtml are disabled so prefix creation does not
    # ask to install Mono and Gecko.
    set(WOLF_TEST_ENV
        "WINEPREFIX=${WOLF_WINEPREFIX}"
        "WINEDEBUG=-all"
        "WINEDLLOVERRIDES=dinput8=n,b;mscoree,mshtml=")

    # wine.start stops a server left over from an interrupted run, starts one that exits by itself 10 seconds after its last client, and boots the
    # prefix (creating it on the first run), so Wine's service processes are already up when the tests start. Those daemons keep every inherited file
    # descriptor open, and CTest waits for its output pipe to close, so nothing they inherit may be that pipe.
    add_test(NAME wine.start COMMAND sh -c [=[
        "$0" -k 2>/dev/null
        "$0" -p10 </dev/null >/dev/null 2>&1 || exit 1
        exec "$1" wineboot </dev/null >/dev/null 2>&1
    ]=] "${WINESERVER_EXECUTABLE}" "${WINE_EXECUTABLE}")
    set_tests_properties(wine.start PROPERTIES FIXTURES_SETUP wine TIMEOUT 600 ENVIRONMENT "${WOLF_TEST_ENV}")
endif()

# Compile-only: the public headers as C11, with the ABI pinned.
add_library(wolf_c_header OBJECT test_c_header.c)
target_link_libraries(wolf_c_header PRIVATE wolf_sdk)

add_subdirectory(unit)
add_subdirectory(game)
