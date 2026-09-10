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

# ---- spike: minimal RGB565 renderer (framebuffer + font + damage + panel) ----
# No LVGL/SDL/OS deps; the draw layer the target (M33) builds.
add_library(spike STATIC spike/fb.cc spike/font.cc spike/damage.cc spike/panel.cc)
target_include_directories(spike PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/spike)
target_link_libraries(spike PUBLIC engine scope)

# ---- Vendored LVGL (git submodule) ----
# Trim to the core library: no demos, examples, or ThorVG.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/controller/lv_conf.h" CACHE PATH
    "Path to lv_conf.h" FORCE)
add_subdirectory(lvgl)

# ---- Vendored RtMidi (sim-only MIDI input, Linux/ALSA) ----
find_package(ALSA REQUIRED)
add_library(rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi)
target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
target_compile_options(rtmidi PRIVATE -fexceptions)  # RtMidi throws RtMidiError
target_link_libraries(rtmidi PUBLIC ALSA::ALSA)

# ---- Controller: portable display + control (LVGL UI + scope/fft helpers) ----
# No SDL/RtMidi/miniaudio — this is the library the target (M33) builds.
add_library(controller STATIC controller/ui.cc)
target_include_directories(controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/controller)
target_link_libraries(controller PUBLIC engine scope lvgl)

# ---- Desktop host: SDL + RtMidi + miniaudio wiring around the controller ----
find_package(SDL2 REQUIRED)
add_executable(host host/main.cc host/midi_io.cc)
target_include_directories(host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/host)
target_compile_options(host PRIVATE -fexceptions)  # host/midi_io.cc catches RtMidiError
target_link_libraries(host PRIVATE controller audio rtmidi lvgl ${SDL2_LIBRARIES})

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

# ---- Tests ----
enable_testing()
add_executable(test_engine tests/test_engine.cc)
target_link_libraries(test_engine PRIVATE engine)
add_test(NAME engine_render COMMAND test_engine)

add_executable(test_params tests/test_params.cc)
target_link_libraries(test_params PRIVATE engine)
add_test(NAME params COMMAND test_params)

find_package(Threads REQUIRED)
add_executable(test_ring tests/test_ring.cc)
target_link_libraries(test_ring PRIVATE engine)
add_test(NAME ring COMMAND test_ring)

add_executable(test_param_block tests/test_param_block.cc)
target_link_libraries(test_param_block PRIVATE engine)
add_test(NAME param_block COMMAND test_param_block)

add_executable(test_split tests/test_split.cc)
target_link_libraries(test_split PRIVATE engine Threads::Threads)
add_test(NAME split COMMAND test_split)

add_executable(test_allocator tests/test_allocator.cc)
target_link_libraries(test_allocator PRIVATE engine)
add_test(NAME allocator COMMAND test_allocator)

add_executable(test_midi tests/test_midi.cc)
target_link_libraries(test_midi PRIVATE engine)
add_test(NAME midi COMMAND test_midi)

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
target_link_libraries(test_panel PRIVATE spike)
add_test(NAME panel COMMAND test_panel)
