cmake_minimum_required(VERSION 3.20)
project(MegaMan5Recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(NESRECOMP_GAME_VERSION "0.1.0-development" CACHE STRING "Game release version")
set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp checkout")
set(NESRECOMP_BACKEND "cycle" CACHE STRING "CPU backend")
set(NESRECOMP_ROM "" CACHE FILEPATH "Original Mega Man 5 USA ROM")
if(NOT NESRECOMP_BACKEND STREQUAL "cycle")
    message(FATAL_ERROR "Mega Man 5 requires the cycle backend")
endif()
if(NOT EXISTS "${NESRECOMP_ROM}")
    message(FATAL_ERROR "Supply -DNESRECOMP_ROM with your Mega Man 5 (USA) ROM")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}"
    RESULT_VARIABLE profile_result OUTPUT_VARIABLE profile OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT profile_result EQUAL 0)
    message(FATAL_ERROR "Mega Man 5 ROM profile validation failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}")
include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
nesrecomp_add_cycle_game(MegaMan5Recomp
    ROM "${NESRECOMP_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    GAME_ID "mega-man-5-usa"
    PLAYERS 1
    REGION NTSC
    MODS)
