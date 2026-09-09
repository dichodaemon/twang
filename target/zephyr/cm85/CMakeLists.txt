# twang — Cortex-M85 audio-core image.
#
# Builds the portable engine (engine/) into a Zephyr application for the
# EK-RA8D2. No desktop dependencies (SDL/RtMidi/miniaudio) here; the control
# (M33) image is a separate application.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(twang_cm85)

# Portable audio engine — the C++ core shared with the desktop build.
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../engine)

target_sources(app PRIVATE
    src/main.cc
    ${ENGINE_DIR}/allocator.cc
    ${ENGINE_DIR}/engine.cc
    ${ENGINE_DIR}/ipc.cc
    ${ENGINE_DIR}/midi.cc
    ${ENGINE_DIR}/params.cc
)

target_include_directories(app PRIVATE ${ENGINE_DIR})
