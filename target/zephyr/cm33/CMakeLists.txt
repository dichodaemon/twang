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
    ${NOSTROMO_DIR}/interaction.cc
    ${NOSTROMO_DIR}/pages.cc
    ${NOSTROMO_DIR}/feel.cc
    ${NOSTROMO_DIR}/surface.cc
    ${NOSTROMO_DIR}/clock_zephyr.cc
    ${CONTROLLER_DIR}/fft.cc
    ${CONTROLLER_DIR}/scope_ring.cc
    ${ENGINE_DIR}/allocator.cc
    ${ENGINE_DIR}/engine_control.cc
    ${ENGINE_DIR}/ipc.cc
    ${ENGINE_DIR}/midi.cc
    ${ENGINE_DIR}/params.cc
)

target_include_directories(app PRIVATE ${ENGINE_DIR} ${CONTROLLER_DIR} ${SPIKE_DIR} ${NOSTROMO_DIR})
