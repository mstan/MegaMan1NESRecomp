cmake_minimum_required(VERSION 3.20)
project(MegaManRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp engine checkout")
set(NESRECOMP_BACKEND "cycle" CACHE STRING "CPU backend: cycle or legacy")
set_property(CACHE NESRECOMP_BACKEND PROPERTY STRINGS cycle legacy)
option(MM1_TEST_NMI_CONTINUATION "Test build: inject one native wait in a reflected-shot continuation" OFF)
if(MM1_TEST_NMI_CONTINUATION)
    set_source_files_properties(mm1_reflect.c PROPERTIES COMPILE_DEFINITIONS MM1_TEST_NMI_CONTINUATION=1)
endif()
if(NESRECOMP_BACKEND STREQUAL "cycle")
    set(NESRECOMP_ROM "" CACHE FILEPATH "Original North American Mega Man ROM (explicit EU input also supported)")
    if(NOT NESRECOMP_ROM AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Mega Man (USA).nes")
        set(NESRECOMP_ROM "${CMAKE_CURRENT_SOURCE_DIR}/Mega Man (USA).nes" CACHE FILEPATH "Original Mega Man ROM" FORCE)
    endif()
    if(NOT EXISTS "${NESRECOMP_ROM}")
        message(FATAL_ERROR "Supply Mega Man (USA).nes or set NESRECOMP_ROM explicitly; EU qualification requires an explicit ROM path")
    endif()
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    set(_proto_assets "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded/packages/mega-man-usa.character.protoman/1.0.0/assets")
    file(READ "${_proto_assets}/protoman.png" MM1_PROTO_PNG_BYTES HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM1_PROTO_PNG_BYTES "${MM1_PROTO_PNG_BYTES}")
    file(READ "${_proto_assets}/jump-open.png" MM1_PROTO_JUMP_PNG_BYTES HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM1_PROTO_JUMP_PNG_BYTES "${MM1_PROTO_JUMP_PNG_BYTES}")
    configure_file(mm1_proto_sheet.h.in "${CMAKE_CURRENT_BINARY_DIR}/mm1_proto_sheet.h" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_proto_assets}/protoman.png" "${_proto_assets}/jump-open.png")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}"
        RESULT_VARIABLE profile_result OUTPUT_VARIABLE profile OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT profile_result EQUAL 0)
        message(FATAL_ERROR "Mega Man ROM profile validation failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}")
    list(GET profile 0 megaman_region)
    list(GET profile 1 megaman_variant)
    # Select this after identifying the ROM on every configure. A cached EU
    # path must not survive switching the same build directory to a USA ROM.
    set(MEGAMAN_CYCLE_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/cyc_seeds_${megaman_variant}.txt")
    set(NESRECOMP_HOST_COMPILER "" CACHE FILEPATH "Optional prebuilt host compiler")
    if(NOT EXISTS "${NESRECOMP_ROOT}/runner/cyc/project.cmake")
        message(FATAL_ERROR "Mega Man regional builds require the current cycle engine; set NESRECOMP_ROOT to a current NESRecomp checkout. Existing engine worktree changes are preserved.")
    endif()
    message(STATUS "Mega Man: ${megaman_variant} payload, ${megaman_region} timing")
    include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
    set(MEGAMAN_GAME_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/game.toml")
    set(MEGAMAN_WIDESCREEN_ARGS)
    # Each regional cartridge has its own verified instruction boundaries.
    if(megaman_variant STREQUAL "usa")
        set(MEGAMAN_HOOKS "${CMAKE_CURRENT_SOURCE_DIR}/widescreen_hooks_usa.toml")
        set(MEGAMAN_PLAYERS 2)
    else()
        set(MEGAMAN_HOOKS "${CMAKE_CURRENT_SOURCE_DIR}/widescreen_hooks.toml")
        set(MEGAMAN_PLAYERS 1)
    endif()
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/game.toml" megaman_game)
        file(READ "${MEGAMAN_HOOKS}" megaman_wide_hooks)
        set(MEGAMAN_GAME_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/game-widescreen.toml")
        file(WRITE "${MEGAMAN_GAME_CONFIG}" "${megaman_game}\n${megaman_wide_hooks}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
            "${MEGAMAN_HOOKS}")
        list(APPEND MEGAMAN_WIDESCREEN_ARGS
            MODS HOST_EXTRAS "${CMAKE_CURRENT_SOURCE_DIR}/game_widescreen.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_host.c" "${CMAKE_CURRENT_SOURCE_DIR}/mm1_weapons.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_proto.c" "${CMAKE_CURRENT_SOURCE_DIR}/mm1_proto_video.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_reflect.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_charge.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_slide.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_coop.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_weapon_gfx.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/mm1_coop_menu.c"
            "${NESRECOMP_ROOT}/runner/cyc/cyc_protoman_atlas.c"
            "${NESRECOMP_ROOT}/runner/cyc/cyc_mm4_donor.c"
            "${NESRECOMP_ROOT}/runner/cyc/cyc_mm4_audio.c"
            "${NESRECOMP_ROOT}/runner/cyc/cyc_mm4_charge_data.c")
    nesrecomp_add_cycle_game(MegaManRecomp
        ROM "${NESRECOMP_ROM}" GAME_CONFIG "${MEGAMAN_GAME_CONFIG}"
        RECOMPILER "${NESRECOMP_HOST_COMPILER}" SEED_FILE "${MEGAMAN_CYCLE_SEEDS}"
        GAME_ID "mega-man-${megaman_variant}" PLAYERS ${MEGAMAN_PLAYERS} REGION "${megaman_region}"
        BOXART "${CMAKE_CURRENT_SOURCE_DIR}/boxart.tga" ${MEGAMAN_WIDESCREEN_ARGS})
    if(megaman_variant STREQUAL "usa")
        target_compile_definitions(MegaManRecomp PRIVATE MM1_PROFILE_USA=1)
    endif()
    target_include_directories(MegaManRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" "${NESRECOMP_ROOT}/runner/src")
    add_custom_command(TARGET MegaManRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded"
            "$<TARGET_FILE_DIR:MegaManRecomp>/mods")
    return()
elseif(NOT NESRECOMP_BACKEND STREQUAL "legacy")
    message(FATAL_ERROR "NESRECOMP_BACKEND must be cycle or legacy")
endif()
include(${NESRECOMP_ROOT}/runner/runner.cmake)

list(APPEND CMAKE_PREFIX_PATH "${NESRECOMP_ROOT}/runner/external/SDL2/cmake")
find_package(SDL2 REQUIRED)

file(GLOB MEGA_MAN_GEN CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/generated/mega-man_full*.c"
    "${CMAKE_SOURCE_DIR}/generated/mega-man_dispatch.c"
)

add_executable(MegaManRecomp
    ${NESRECOMP_RUNNER_SOURCES}
    extras.c
    ${MEGA_MAN_GEN}
)

target_compile_definitions(MegaManRecomp PRIVATE NESRECOMP_SPLIT_PARTS_EXTERNAL)

target_include_directories(MegaManRecomp PRIVATE
    ${NESRECOMP_RUNNER_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(MegaManRecomp PRIVATE ${SDL2_LIBRARIES})

if(WIN32)
    target_link_libraries(MegaManRecomp PRIVATE ws2_32)
    add_custom_command(TARGET MegaManRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NESRECOMP_ROOT}/runner/external/SDL2/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:MegaManRecomp>"
    )
endif()

if(MSVC)
    target_compile_options(MegaManRecomp PRIVATE /W3 /WX- /wd4102 /MP4)
endif()

set(NESRECOMP_RECOMP_UI "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
include("${NESRECOMP_RECOMP_UI}/recomp_ui.cmake")
recomp_target_launcher_ui(MegaManRecomp CONSOLE nes)
