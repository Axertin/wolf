# wolf_add_mod(<id> SOURCES <files...> [FILES <files copied beside the DLL>])
#
# Builds the code mod <id> as the SHARED target <id>, whose DLL is <id>.dll. It links wolf_sdk for <wolf/wolf.h>. Under MSVC it builds its C++ with exceptions
# on (/EHsc) and uses the Visual C++ Redistributable's runtime DLLs, so a player whose redistributable is older than the toolset updates it. Under MinGW it
# links the toolchain's C++ runtime in, since no player has libc++ to load. When WOLF_MODS_DIR is set, the DLL lands in ${WOLF_MODS_DIR}/<id>/, the layout WOLF
# reads from the game's mods directory. FILES, such as mod.ini and config.ini, are copied beside the DLL whenever the mod builds, so an edit to one lands on the
# next build; a relative path is relative to the calling CMakeLists.txt. C++ sources build as C++20 or later.

# The SDK headers sit one directory up from this file. A build that already defines wolf_sdk, as WOLF's own does, keeps its definition.
if(NOT TARGET wolf_sdk)
    add_library(wolf_sdk INTERFACE)
    target_include_directories(wolf_sdk INTERFACE "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

function(wolf_add_mod id)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;FILES")
    add_library(${id} SHARED ${arg_SOURCES})
    set_target_properties(${id} PROPERTIES PREFIX "")
    target_link_libraries(${id} PRIVATE wolf_sdk)
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    if("CXX" IN_LIST languages)
        # <wolf/wolf.hpp> needs C++20.
        target_compile_features(${id} PRIVATE cxx_std_20)
    endif()
    if(MSVC)
        target_compile_options(${id} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    elseif(MINGW)
        target_link_options(${id} PRIVATE -static)
    endif()
    if(WOLF_MODS_DIR)
        # $<0:> keeps multi-config generators from adding a per-configuration subdirectory.
        set_target_properties(${id} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${WOLF_MODS_DIR}/${id}$<0:>")
    endif()
    if(arg_FILES)
        # A custom target is always out of date, so every build of the mod runs it, and copy_if_different picks up an edited file.
        add_custom_target(${id}_files
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${id}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${arg_FILES} "$<TARGET_FILE_DIR:${id}>"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
        add_dependencies(${id} ${id}_files)
    endif()
endfunction()
