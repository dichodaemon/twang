cmake_minimum_required(VERSION 3.16)
project(synth LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Embedded-style C++: no exceptions, no RTTI (unwind tables and type info
# cost flash/RAM and are unnecessary in the audio path).
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

# Emit build/compile_commands.json so clangd/editors get real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ThreadSanitizer build, for verifying the control/audio split (test_split).
option(TWANG_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(TWANG_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# ---- Engine: no LVGL/SDL/OS dependencies ----
add_library(engine STATIC
    engine/allocator.cc
    engine/engine.cc
    engine/engine_audio.cc
    engine/engine_control.cc
    engine/ipc.cc
    engine/midi.cc
    engine/params.cc
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine)

# ---- Audio output abstraction (realtime device backend) ----
# Hides the device library (miniaudio) behind audio::Output; the host only sees
# a render callback, so the backend can be swapped for a hardware driver.
add_library(audio STATIC audio/audio_out_miniaudio.cc)
target_include_directories(audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/audio)
target_include_directories(audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)
target_link_libraries(audio PUBLIC m pthread dl)

# ---- scope: lvgl-free display helpers (FFT + scope ring) ----
add_library(scope STATIC controller/fft.cc controller/scope_ring.cc)
target_include_directories(scope PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/controller)

# ---- spike: minimal RGB565 renderer (framebuffer + font + damage + PNG) ----
# General and HW-agnostic: no project vocabulary, no engine/scope dependency.
add_library(spike STATIC spike/fb.cc spike/font.cc spike/damage.cc spike/png.cc)
target_include_directories(spike PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/spike)

# ---- nostromo: this synth's GUI (panel + vocabulary), built on spike ----
add_library(nostromo STATIC nostromo/panel.cc nostromo/pages.cc nostromo/feel.cc nostromo/surface.cc nostromo/interaction.cc)
target_include_directories(nostromo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nostromo)
target_link_libraries(nostromo PUBLIC spike engine scope)

# ---- Desktop simulator (SDL2 + ALSA/RtMidi) ----
# Only `host` and `midi_dump` need these. They were found at file scope, which
# made SDL2 and ALSA configure-time preconditions for the whole project: on a
# machine without libasound2-dev, cmake aborted before generating anything, so
# the renderers, the mockups and the entire ctest suite were unbuildable for
# want of a MIDI library none of them use. Scoping the lookups to the targets
# that need them lets the project configure anywhere.
#
# Turning this OFF drops `host` and `midi_dump` and nothing else.
option(TWANG_BUILD_HOST "Build the desktop simulator (needs SDL2 and ALSA)" ON)

if(TWANG_BUILD_HOST)
    # ---- Vendored RtMidi (sim-only MIDI input, Linux/ALSA) ----
    find_package(ALSA REQUIRED)
    add_library(rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
    target_include_directories(rtmidi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi)
    target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
    target_compile_options(rtmidi PRIVATE -fexceptions)  # RtMidi throws RtMidiError
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA)

    # ---- Minimal MIDI dump: raw input via RtMidi's callback API ----
    # Reference reader for diagnosing surface behaviour; agrees with aseqdump.
    add_executable(midi_dump tools/midi_dump.cc)
    target_compile_options(midi_dump PRIVATE -fexceptions)  # RtMidi throws RtMidiError
    target_link_libraries(midi_dump PRIVATE rtmidi)

    # ---- Desktop host: SDL + RtMidi + miniaudio around the spike panel ----
    find_package(SDL2 REQUIRED)
    add_executable(host host/main.cc host/midi_io.cc host/sdl_backend.cc)
    target_include_directories(host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/host)
    target_include_directories(host PRIVATE ${SDL2_INCLUDE_DIRS})
    target_compile_options(host PRIVATE -fexceptions)  # host/midi_io.cc catches RtMidiError
    target_link_libraries(host PRIVATE nostromo audio rtmidi ${SDL2_LIBRARIES})
else()
    message(STATUS "TWANG_BUILD_HOST=OFF: skipping host and midi_dump")
endif()

# ---- WAV renderer: hear the engine output ----
add_executable(wav_render tools/wav_render.cc)
target_link_libraries(wav_render PRIVATE engine)

# ---- Live audio: stream the engine to the playback device (miniaudio) ----
add_executable(live_render tools/live_render.cc)
target_include_directories(live_render PRIVATE third_party/miniaudio)
target_link_libraries(live_render PRIVATE engine m pthread dl)

# ---- Cycle harness: measure render cost ----
add_executable(bench tools/bench.cc)
target_link_libraries(bench PRIVATE engine)

# ---- Offscreen panel screenshot: golden-image fixture (no SDL/display) ----
add_executable(panel_shot tools/panel_shot.cc)
target_link_libraries(panel_shot PRIVATE nostromo)

# ---- Tests ----
enable_testing()
add_executable(test_engine tests/test_engine.cc)
target_link_libraries(test_engine PRIVATE engine)
add_test(NAME engine_render COMMAND test_engine)

add_executable(test_shaper tests/test_shaper.cc)
target_link_libraries(test_shaper PRIVATE engine)
add_test(NAME shaper COMMAND test_shaper)

add_executable(test_params tests/test_params.cc)
target_link_libraries(test_params PRIVATE engine)
add_test(NAME params COMMAND test_params)

find_package(Threads REQUIRED)
add_executable(test_ring tests/test_ring.cc)
target_link_libraries(test_ring PRIVATE engine)
add_test(NAME ring COMMAND test_ring)

add_executable(test_scope_ring tests/test_scope_ring.cc)
target_link_libraries(test_scope_ring PRIVATE scope Threads::Threads)
add_test(NAME scope_ring COMMAND test_scope_ring)

add_executable(test_param_block tests/test_param_block.cc)
target_link_libraries(test_param_block PRIVATE engine)
add_test(NAME param_block COMMAND test_param_block)

add_executable(test_param_atomic tests/test_param_atomic.cc)
target_link_libraries(test_param_atomic PRIVATE engine Threads::Threads)
add_test(NAME param_atomic COMMAND test_param_atomic)

add_executable(test_split tests/test_split.cc)
target_link_libraries(test_split PRIVATE engine Threads::Threads)
add_test(NAME split COMMAND test_split)

add_executable(test_allocator tests/test_allocator.cc)
target_link_libraries(test_allocator PRIVATE engine)
add_test(NAME allocator COMMAND test_allocator)

add_executable(test_mod_route tests/test_mod_route.cc)
target_link_libraries(test_mod_route PRIVATE engine)
add_test(NAME mod_route COMMAND test_mod_route)

add_executable(test_fb tests/test_fb.cc)
target_link_libraries(test_fb PRIVATE spike)
add_test(NAME fb COMMAND test_fb)

add_executable(test_font tests/test_font.cc)
target_link_libraries(test_font PRIVATE spike)
add_test(NAME font COMMAND test_font)

add_executable(test_damage tests/test_damage.cc)
target_link_libraries(test_damage PRIVATE spike)
add_test(NAME damage COMMAND test_damage)

add_executable(test_panel tests/test_panel.cc)
target_link_libraries(test_panel PRIVATE nostromo)
add_test(NAME panel COMMAND test_panel)

add_executable(test_panel_pages tests/test_panel_pages.cc)
target_link_libraries(test_panel_pages PRIVATE nostromo)
add_test(NAME panel_pages COMMAND test_panel_pages)

add_executable(test_bindings tests/test_bindings.cc)
target_link_libraries(test_bindings PRIVATE nostromo)
add_test(NAME bindings COMMAND test_bindings)

add_executable(test_gestures tests/test_gestures.cc)
target_link_libraries(test_gestures PRIVATE nostromo)
add_test(NAME gestures COMMAND test_gestures)

add_executable(test_surface tests/test_surface.cc)
target_link_libraries(test_surface PRIVATE nostromo)
add_test(NAME surface COMMAND test_surface)

add_executable(test_interaction tests/test_interaction.cc)
target_link_libraries(test_interaction PRIVATE nostromo)
add_test(NAME interaction COMMAND test_interaction)

add_executable(test_midi_ring tests/test_midi_ring.cc)
target_link_libraries(test_midi_ring PRIVATE scope)
add_test(NAME midi_ring COMMAND test_midi_ring)
