cmake_minimum_required(VERSION 3.20)
project(MegaMan5Recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(NESRECOMP_GAME_VERSION "0.2.0" CACHE STRING "Game release version")
set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp checkout")
set(NESRECOMP_BACKEND "cycle" CACHE STRING "CPU backend")
set(NESRECOMP_ROM "" CACHE FILEPATH "Original Mega Man 5 USA ROM")
set(NESRECOMP_HOST_COMPILER "" CACHE FILEPATH "Optional existing host NESRecomp compiler")
if(NOT NESRECOMP_BACKEND STREQUAL "cycle")
    message(FATAL_ERROR "Mega Man 5 requires the cycle backend")
endif()
if(NOT EXISTS "${NESRECOMP_ROM}")
    message(FATAL_ERROR "Supply -DNESRECOMP_ROM with your Mega Man 5 (USA) ROM")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(mm5_art "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded/packages/mega-man-5-usa.character.protoman/1.0.0/assets")
file(READ "${mm5_art}/protoman.png" MM5_PROTO_PNG_BYTES HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM5_PROTO_PNG_BYTES "${MM5_PROTO_PNG_BYTES}")
file(READ "${mm5_art}/jump-open.png" MM5_PROTO_JUMP_PNG_BYTES HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM5_PROTO_JUMP_PNG_BYTES "${MM5_PROTO_JUMP_PNG_BYTES}")
configure_file(src/mm5_proto_sheet.h.in "${CMAKE_CURRENT_BINARY_DIR}/mm5_proto_sheet.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mm5_art}/protoman.png" "${mm5_art}/jump-open.png")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}"
    RESULT_VARIABLE profile_result OUTPUT_VARIABLE profile OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT profile_result EQUAL 0)
    message(FATAL_ERROR "Mega Man 5 ROM profile validation failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}")
include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
set(mm5_compiler_args)
if(NESRECOMP_HOST_COMPILER)
    list(APPEND mm5_compiler_args RECOMPILER "${NESRECOMP_HOST_COMPILER}")
endif()
nesrecomp_add_cycle_game(MegaMan5Recomp
    ROM "${NESRECOMP_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    GAME_ID "mega-man-5-usa"
    PLAYERS 2
    REGION NTSC
    ${mm5_compiler_args}
    MODS
    HOST_EXTRAS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_host.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_net.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_pool.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_cart.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_world.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_coop.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_hud.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_menu.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_wide.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_weapons.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_proto.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mm5_view.c"
        "${NESRECOMP_ROOT}/runner/cyc/cyc_protoman_atlas.c")
target_include_directories(MegaMan5Recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}" "${NESRECOMP_ROOT}/runner/src")
option(MM5_ENABLE_NETPLAY "Build the shared rollback netplay and lobby" ON)
option(MM5_NETPLAY_QUALIFIED "Enable the qualified MM5 simulation profile" ON)
if(MM5_ENABLE_NETPLAY OR NESRECOMP_ENABLE_NET)
    nesrecomp_enable_recomp_net(MegaMan5Recomp)
    if(MM5_NETPLAY_QUALIFIED)
        target_compile_definitions(MegaMan5Recomp PRIVATE MM5_NETPLAY_QUALIFIED=1)
    endif()
endif()
option(MM5_NETPLAY_TEST_BOOT "Allow exact current-build preadmission QA fixtures" OFF)
if(MM5_NETPLAY_TEST_BOOT)
    set_property(SOURCE "${NESRECOMP_ROOT}/runner/cyc/cyc_net.c" APPEND PROPERTY COMPILE_DEFINITIONS NESRECOMP_NET_TEST_BOOT=1)
endif()
add_custom_command(TARGET MegaMan5Recomp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded/packages"
        "$<TARGET_FILE_DIR:MegaMan5Recomp>/mods/packages")
