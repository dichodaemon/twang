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
    engine/engine.cc
    engine/params.cc
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine)

# ---- Vendored LVGL (git submodule) ----
# Trim to the core library: no demos, examples, or ThorVG.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sim/lv_conf.h" CACHE PATH
    "Path to lv_conf.h" FORCE)
add_subdirectory(lvgl)

# ---- Simulator: LVGL + SDL2 ----
find_package(SDL2 REQUIRED)
add_executable(sim sim/main.cc)
target_link_libraries(sim PRIVATE lvgl ${SDL2_LIBRARIES})

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
