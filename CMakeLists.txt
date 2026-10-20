cmake_minimum_required(VERSION 3.16)

# rapture: the shared engine of the Disney games built on the Rapture engine (Moses/Aaron) (platform layer + Moses + Aaron + Arcade + Host).
# Used two ways:
#   * as a subdirectory of a game repo (git submodule at engine/):   add_subdirectory(engine)   then link `rapture`
#   * standalone:   cmake -S engine -B build-engine -G "MinGW Makefiles" -DSDL2_DIR=<sdl2>/cmake
#     (also builds the test executable `rapture_tests`, see tests/TestMain.cpp and docs/README.md)
#
# Options:
#   RAPTURE_BUILD_TESTS   (ON)  compile the headless tests (--engine-test N, --game-test arcade|host, --rrs-test) into the
#                               library (RAPTURE_WITH_TESTS=1 for the users of the library); standalone: also `rapture_tests`.
#   RAPTURE_FORCE_PORTABLE (OFF) compile with every Win32-specific code path disabled (no winmm MIDI output, no GDI text, no Win32 console
#                               attach, Linux-style settings path), as on Linux/macOS, even on Windows / MinGW.  Defines the macro
#                               RAPTURE_FORCE_PORTABLE for the engine and its users (platform/Config.h turns it into "no RAPTURE_WIN32").
project(RaptureEngine CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RAPTURE_STANDALONE ON)
else()
    set(RAPTURE_STANDALONE OFF)
endif()

option(RAPTURE_BUILD_TESTS "Build the headless engine tests" ON)
option(RAPTURE_FORCE_PORTABLE "Disable every Win32-specific code path (as on Linux/macOS), even on Windows/MinGW" OFF)

if(RAPTURE_STANDALONE)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# SDL2: provided by the parent project, or found here (-DSDL2_DIR=<sdl2>/cmake).
if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 REQUIRED CONFIG)
endif()

file(GLOB_RECURSE RAPTURE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/moses/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/aaron/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/arcade/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/autoplay/*.cpp)

# tests/*.cpp: compiled into the library only with RAPTURE_BUILD_TESTS.  tests/TestMain.cpp is the main() of the standalone
# test executable and never goes into the library.
if(RAPTURE_BUILD_TESTS)
    file(GLOB RAPTURE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    list(FILTER RAPTURE_TEST_SOURCES EXCLUDE REGEX "/TestMain[.]cpp$")
    list(APPEND RAPTURE_SOURCES ${RAPTURE_TEST_SOURCES})
endif()

add_library(rapture STATIC ${RAPTURE_SOURCES})
target_include_directories(rapture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rapture PUBLIC SDL2::SDL2)
if(RAPTURE_BUILD_TESTS)
    target_compile_definitions(rapture PUBLIC RAPTURE_WITH_TESTS=1)
endif()

if(RAPTURE_FORCE_PORTABLE)
    target_compile_definitions(rapture PUBLIC RAPTURE_FORCE_PORTABLE=1)
endif()

find_package(Threads)
if(TARGET Threads::Threads)
    target_link_libraries(rapture PUBLIC Threads::Threads)   # the software MIDI sequencer thread
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(rapture PUBLIC m)
endif()

if(WIN32 AND NOT RAPTURE_FORCE_PORTABLE)
    target_link_libraries(rapture PUBLIC gdi32 winmm)
endif()
if(MINGW)
    target_link_options(rapture INTERFACE -static-libgcc -static-libstdc++)
endif()
if(MSVC)
    target_compile_options(rapture PRIVATE /W3)
else()
    target_compile_options(rapture PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Helper for the executables of a game (and of the standalone test build): copy SDL2.dll next to the exe on Windows.
function(rapture_copy_sdl2_dll target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

if(RAPTURE_STANDALONE AND RAPTURE_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestMain.cpp)
    add_executable(rapture_tests WIN32 tests/TestMain.cpp)
    target_link_libraries(rapture_tests PRIVATE rapture SDL2::SDL2main)
    rapture_copy_sdl2_dll(rapture_tests)
endif()
