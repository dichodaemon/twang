cmake_minimum_required(VERSION 3.16)
project(synth LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit build/compile_commands.json so clangd/editors get real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Engine: pure C++, no LVGL/SDL/OS dependencies ----
add_library(engine STATIC
    engine/engine.cpp
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
add_executable(sim sim/main.c)
target_link_libraries(sim PRIVATE lvgl ${SDL2_LIBRARIES})

# ---- WAV renderer: hear the engine output ----
add_executable(wav_render tools/wav_render.c)
target_link_libraries(wav_render PRIVATE engine)

# ---- Live audio: stream the engine to the playback device (miniaudio) ----
add_executable(live_render tools/live_render.c)
target_include_directories(live_render PRIVATE third_party/miniaudio)
target_link_libraries(live_render PRIVATE engine m pthread dl)

# ---- Cycle harness: measure render cost ----
add_executable(bench tools/bench.c)
target_link_libraries(bench PRIVATE engine)

# ---- Tests ----
enable_testing()
add_executable(test_engine tests/test_engine.c)
target_link_libraries(test_engine PRIVATE engine)
add_test(NAME engine_render COMMAND test_engine)
