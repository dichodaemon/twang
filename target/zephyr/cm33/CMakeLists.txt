# twang — Cortex-M33 control-core image.
#
# Builds the portable controller (LVGL UI + scope/fft) and the engine into a
# Zephyr application for the EK-RA8D2. This core drives the 7" 1024x600 RGB
# panel through the GLCDC and runs the LVGL panel UI; the audio (M85) core is
# a separate application.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(twang_cm33)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../engine)
set(CONTROLLER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../controller)

target_sources(app PRIVATE
    src/main.cc
    ${CONTROLLER_DIR}/ui.cc
    ${CONTROLLER_DIR}/fft.cc
    ${CONTROLLER_DIR}/scope_ring.cc
    ${ENGINE_DIR}/allocator.cc
    ${ENGINE_DIR}/engine.cc
    ${ENGINE_DIR}/ipc.cc
    ${ENGINE_DIR}/midi.cc
    ${ENGINE_DIR}/params.cc
)

target_include_directories(app PRIVATE ${ENGINE_DIR} ${CONTROLLER_DIR})

# The engine's control/audio IPC lives in shared SDRAM (see ipc_shared.h).
target_compile_definitions(app PRIVATE TWANG_SHARED_IPC)

# The controller includes "lvgl/lvgl.h" (LVGL 9 root shim). The Zephyr LVGL
# module only exposes <src/>; add the module's parent directory so the root
# shim resolves (lvgl/lvgl.h -> modules/lib/gui/lvgl/lvgl.h).
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/../modules/lib/gui/)
