# twang — Cortex-M33 control-core image.
#
# Builds the portable spike renderer (framebuffer + font + damage + panel),
# the scope helpers, and the engine into a Zephyr application for the
# EK-RA8D2. This core drives the 7" 1024x600 RGB panel through the GLCDC and
# runs the spike panel UI; the audio (M85) core is a separate application.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(twang_cm33)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../engine)
set(CONTROLLER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../controller)
set(SPIKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../spike)
set(NOSTROMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../nostromo)

target_sources(app PRIVATE
    src/main.cc
    src/glcdc_backend.cc
    ${SPIKE_DIR}/fb.cc
    ${SPIKE_DIR}/font.cc
    ${SPIKE_DIR}/damage.cc
    ${NOSTROMO_DIR}/panel.cc
    ${CONTROLLER_DIR}/fft.cc
    ${CONTROLLER_DIR}/scope_ring.cc
    ${ENGINE_DIR}/allocator.cc
    ${ENGINE_DIR}/engine.cc
    ${ENGINE_DIR}/ipc.cc
    ${ENGINE_DIR}/midi.cc
    ${ENGINE_DIR}/params.cc
)

target_include_directories(app PRIVATE ${ENGINE_DIR} ${CONTROLLER_DIR} ${SPIKE_DIR} ${NOSTROMO_DIR})

# The engine's control/audio IPC lives in shared SDRAM (see ipc_shared.h);
# the panel's draw scratch is placement-new'd into SDRAM too (TWANG_UI_SDRAM,
# see nostromo/panel.cc).
target_compile_definitions(app PRIVATE TWANG_SHARED_IPC TWANG_UI_SDRAM)
