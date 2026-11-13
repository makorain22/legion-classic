cmake_minimum_required(VERSION 3.24)
project(LegionClassic VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Legion Classic, built on Slave Engine 1.0
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MinGW: use the C99-compliant printf family (%zu etc.).
if(MINGW)
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

# Static MSVC runtime, so players don't need the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LEGION_BUILD_TESTS "Build the unit tests" ON)
option(LEGION_BUILD_GAME "Build the game executable (needs SDL3)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Third-party code, pinned to exact versions
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(LEGION_BUILD_GAME)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.18
    GIT_SHALLOW    TRUE)

  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.93.0-docking
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(SDL3 imgui)
endif()

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.5.3
  GIT_SHALLOW    TRUE)
FetchContent_Declare(miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        3.1.2
  GIT_SHALLOW    TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json stb miniz)

# glad: OpenGL 3.3 core loader (generated, checked in under external/glad)
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

# stb (header-only; implementation compiled once in the engine)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

if(LEGION_BUILD_GAME)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
endif()

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
function(legion_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4100 /wd4201)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Slave Engine core: deterministic code with no graphics (used by tests too)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SLAVE_CORE_SOURCES CONFIGURE_DEPENDS
  engine/src/core/*.cpp
  engine/src/math/*.cpp
  engine/src/ecs/*.cpp
  engine/src/sim/*.cpp
  engine/src/map/*.cpp)
add_library(slave_core STATIC ${SLAVE_CORE_SOURCES})
target_include_directories(slave_core PUBLIC engine/include)
target_link_libraries(slave_core PUBLIC nlohmann_json::nlohmann_json PRIVATE miniz)
legion_warnings(slave_core)

# ---------------------------------------------------------------------------
# Slave Engine presentation: window, renderer, assets, UI
# ---------------------------------------------------------------------------
if(LEGION_BUILD_GAME)
  file(GLOB_RECURSE SLAVE_GFX_SOURCES CONFIGURE_DEPENDS
    engine/src/platform/*.cpp
    engine/src/render/*.cpp
    engine/src/assets/*.cpp
    engine/src/audio/*.cpp
    engine/src/ui/*.cpp)
  add_library(slave STATIC ${SLAVE_GFX_SOURCES})
  target_include_directories(slave PUBLIC engine/include)
  find_package(Threads REQUIRED)  # sprites are baked on all cores
  target_link_libraries(slave PUBLIC slave_core glad imgui stb SDL3::SDL3-static Threads::Threads)
  if(WIN32)
    target_link_libraries(slave PUBLIC dbghelp)  # crash reports
  endif()
  legion_warnings(slave)

  # -------------------------------------------------------------------------
  # Legion Classic executable
  # -------------------------------------------------------------------------
  file(GLOB_RECURSE LEGION_SOURCES CONFIGURE_DEPENDS game/src/*.cpp editor/src/*.cpp)
  add_executable(LegionClassic WIN32 ${LEGION_SOURCES})
  target_include_directories(LegionClassic PRIVATE game/src editor/src)
  target_link_libraries(LegionClassic PRIVATE slave)
  legion_warnings(LegionClassic)
  if(MSVC)
    # Symbols for crash reports: a .pdb beside the .exe, same optimised code.
    target_compile_options(slave PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_compile_options(slave_core PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_compile_options(LegionClassic PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(LegionClassic PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
  endif()
  set_target_properties(LegionClassic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)

  # Ship the content folder next to the executable.
  add_custom_command(TARGET LegionClassic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/content $<TARGET_FILE_DIR:LegionClassic>/content
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mods $<TARGET_FILE_DIR:LegionClassic>/mods)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LEGION_BUILD_TESTS)
  FetchContent_MakeAvailable(doctest)
  enable_testing()
  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  # Editor logic without graphics (undo history) is tested too.
  # So is the Blitzkrieg file reader (it needs no graphics either).
  # And the sound mixer and decoders (no sound card needed).
  # And the 3D camera and voxel meshing (pure maths, no GL calls).
  add_executable(slave_tests ${TEST_SOURCES} editor/src/history.cpp engine/src/assets/blitzkrieg.cpp
                 engine/src/audio/mixer.cpp engine/src/audio/decode.cpp game/src/campaign.cpp
                 engine/src/render/camera.cpp engine/src/assets/voxel.cpp engine/src/assets/voxel_mesh.cpp
                 engine/src/assets/image.cpp)
  target_include_directories(slave_tests PRIVATE editor/src game/src)
  target_link_libraries(slave_tests PRIVATE slave_core doctest::doctest stb)
  target_compile_definitions(slave_tests PRIVATE LEGION_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content/")
  # Some tests play whole games with the real rules.
  target_compile_definitions(slave_tests PRIVATE LEGION_RULES_DIR="${CMAKE_SOURCE_DIR}/content/rules/")
  legion_warnings(slave_tests)
  add_test(NAME slave_tests COMMAND slave_tests)

  # Builds the shipped maps: tutorial, campaigns, scenarios, skirmish maps (tools/make_content.cpp).
  file(GLOB MAKE_CONTENT_SOURCES CONFIGURE_DEPENDS tools/make_content.cpp tools/campaign_*.cpp)
  add_executable(make_content EXCLUDE_FROM_ALL ${MAKE_CONTENT_SOURCES})
  target_link_libraries(make_content PRIVATE slave_core)
  legion_warnings(make_content)
endif()
