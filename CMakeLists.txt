cmake_minimum_required(VERSION 3.20)
project(Mother3Recomp C CXX)

# Windows resource compiler — required to embed the per-variant .ico below.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────
# Mother3Recomp — static recompilation of MOTHER 3 (GBA) on gbarecomp.
#
# Forked from EmeraldRecomp's per-variant scaffold. Per-variant data lives
# under variants/<name>/ (game.toml + config/ + symbols/ + generated/ + roms/).
# The emerald_* sources below are inherited from that fork; they are
# hash-gated to Emerald and inert for MOTHER 3.
#
#   Target            Game                     ROM       Debug port
#   Mother3Recomp     MOTHER 3 (JPN)           4f0f493e  19893
#   Mother3RecompEN   MOTHER 3 (EN fan patch)  306fb887  19894
# ─────────────────────────────────────────────────────────────────────

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to the gbarecomp platform-core checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
set(GBARECOMP_ENABLE_MODS OFF CACHE BOOL
    "Mod catalog and trusted-plugin runtime — off during bring-up" FORCE)

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT does not point to a gbarecomp checkout:\n"
        "  ${GBARECOMP_ROOT}\n"
        "Point it at the engine checkout (default ../gbarecomp) or pass\n"
        "-DGBARECOMP_ROOT=<path> at configure time.")
endif()

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

add_executable(verify_rom_hash tools/verify_rom_hash/main.cpp)

enable_testing()

# add_gba_variant(<target> <name> <port> <win_title> <game_name> <sha1>
#                  <crc32>)
function(add_gba_variant TARGET NAME PORT WIN_TITLE GAME_NAME ROM_SHA1 ROM_CRC32)
    set(VARIANT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/variants/${NAME}")
    if(EXISTS "${VARIANT_DIR}/generated/recompiled.cpp")
        message(FATAL_ERROR
            "${NAME}: stale monolithic generated/recompiled.cpp found. "
            "Regenerate with the current gba_recompile before building.")
    endif()
    file(GLOB VARIANT_GENERATED CONFIGURE_DEPENDS
        "${VARIANT_DIR}/generated/*.c"
        "${VARIANT_DIR}/generated/*.cpp")

    set(VARIANT_GAME_SOURCES src/main.cpp)
    if(GBARECOMP_ENABLE_MODS)
        list(APPEND VARIANT_GAME_SOURCES
            src/emerald_extended_view.cpp
            src/emerald_object_view.cpp
            src/mods/emerald_adaptive_view_plugin.cpp
            src/emerald_ui_view.cpp
            src/mods/emerald_absolute_rtc_plugin.cpp)
    endif()
    
    # Per-variant application icon. Drop an .ico at
    # variants/<name>/launcher/icon.ico and it gets compiled into a Windows
    # resource and linked in, so Explorer shows it on the executable.
    # Resource id 1: Windows uses the lowest-numbered icon as the app icon.
    # The .rc is compiled in its own OBJECT library so windres does not inherit
    # the game target's compile definitions (e.g. GBARECOMP_BUILTIN_NAME=
    # "MOTHER 3 (EN)") and include paths. windres hands those to its gcc
    # preprocessor through a shell command line, and values/paths with spaces
    # or parentheses make it fail with "preprocessing failed".
    set(VARIANT_ICON "${VARIANT_DIR}/launcher/icon.ico")
    set(VARIANT_ICON_OBJS "")
    if(WIN32 AND EXISTS "${VARIANT_ICON}")
        set(VARIANT_RC "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_icon.rc")
        file(WRITE "${VARIANT_RC}" "1 ICON \"${VARIANT_ICON}\"\n")
        add_library(${TARGET}_icon OBJECT "${VARIANT_RC}")
        set(VARIANT_ICON_OBJS $<TARGET_OBJECTS:${TARGET}_icon>)
        message(STATUS "${NAME}: embedding icon ${VARIANT_ICON}")
    endif()

    add_executable(${TARGET}
        ${VARIANT_GAME_SOURCES}
        ${VARIANT_ICON_OBJS}
        ${VARIANT_GENERATED})

    if("${NAME}" STREQUAL "emerald")
        target_compile_definitions(${TARGET} PRIVATE GBARECOMP_VARIANT_EMERALD=1)
    endif()

    # Keep generated guest translation units at a bounded optimization and
    # debug-info cost. gba_recompile emits stable recompiled_NNN.cpp shards.
    set(VARIANT_RECOMPILED_SOURCES ${VARIANT_GENERATED})
    list(FILTER VARIANT_RECOMPILED_SOURCES INCLUDE REGEX
        "/recompiled_[0-9]+\\.cpp$")
    if(VARIANT_RECOMPILED_SOURCES)
        set_source_files_properties(${VARIANT_RECOMPILED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-O1;-g0")
    endif()

    target_include_directories(${TARGET} PRIVATE
        "${GBARECOMP_ROOT}/src/armv4t"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/debug"
        src)

    target_link_libraries(${TARGET} PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime
        gbarecomp_debug
        gbarecomp_gba
        gbarecomp_armv4t
        "-Wl,--end-group")

    target_compile_definitions(${TARGET} PRIVATE
        GBARECOMP_DEFAULT_DEBUG_PORT=${PORT}
        GBARECOMP_WINDOW_TITLE="${WIN_TITLE}"
        GBARECOMP_DEFAULT_GAME_CONFIG="variants/${NAME}/game.toml"
        GBARECOMP_BUILTIN_NAME="${GAME_NAME}"
        GBARECOMP_BUILTIN_SHA1="${ROM_SHA1}"
        GBARECOMP_BUILTIN_CRC32=${ROM_CRC32}
        GBARECOMP_BUILTIN_REGION="USA")

    if(GBAGAME_RECOMP_UI)
        target_link_libraries(${TARGET} PRIVATE game_launcher_ui)
        # Per-variant box art: variants share one build dir, so each stages
        # its own file and points its exe at it (RunOptions.launcher_boxart).
        set(VARIANT_BOXART "${VARIANT_DIR}/launcher/boxart.tga")
        if(EXISTS "${VARIANT_BOXART}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:${TARGET}>/assets/img
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VARIANT_BOXART}"
                    $<TARGET_FILE_DIR:${TARGET}>/assets/img/boxart_${NAME}.tga
                VERBATIM)
            target_compile_definitions(${TARGET} PRIVATE
                GBARECOMP_BOXART="assets/img/boxart_${NAME}.tga")
        endif()
    endif()

    # 256 MB stack reserve — see FireRedRecomp/CMakeLists.txt rationale
    # (Win64 SEH unwind table scales with ~90k recompiled functions).
    if(MSVC)
        target_link_options(${TARGET} PRIVATE /STACK:268435456)
    elseif(MINGW OR WIN32)
        target_link_options(${TARGET} PRIVATE -Wl,--stack,268435456)
    endif()
endfunction()

# ─────────────────────────────────────────────────────────────────────
# recomp-ui pre-boot launcher (Dear ImGui), consumed as a git submodule
# (junctioned locally to the shared checkout). Isolated in its own
# static lib so recomp-ui's sources + RECOMP_LAUNCHER define never
# touch the giant generated/*.c TU flags — only main.cpp (gated on
# GBAGAME_RECOMP_UI) and launcher_boot.cpp know the launcher exists.
# ─────────────────────────────────────────────────────────────────────
option(GBAGAME_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)
if(GBAGAME_RECOMP_UI)
    include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
    add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp)
    target_include_directories(game_launcher_ui PRIVATE
        "${GBARECOMP_ROOT}/src/runtime"   # launcher_seam.h + runtime.h
        "${GBARECOMP_ROOT}/src/gba"
        src
    )
    if(GBARECOMP_ENABLE_MODS)
        target_compile_definitions(game_launcher_ui PRIVATE
            GBARECOMP_ENABLE_MODS=1)
    endif()
    recomp_target_launcher_ui(game_launcher_ui CONSOLE gba)
    find_package(SDL2 REQUIRED)
    target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
    if(WIN32)
        target_link_libraries(game_launcher_ui PUBLIC opengl32)
    endif()
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS GBAGAME_RECOMP_UI)
endif()

# MOTHER 3 — Japan, sha1 4f0f493e…
add_gba_variant(Mother3Recomp mother3 19893 "Mother3Recomp"
    "MOTHER 3 (JPN)" "4f0f493e12c2a8c61b2d809af03f7abf87a85776" 0x42AC9CB9)

add_gba_variant(Mother3RecompEN mother3en 19894 "Mother3Recomp (EN)"
    "MOTHER 3 (EN)" "306FB8874533B0FD0796208FAA8BED2DB4AE77FB" 0x8A3BC5A8)
