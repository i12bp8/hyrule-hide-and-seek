cmake_minimum_required(VERSION 3.26)

if (NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
endif ()

# The version shown in the mod manager. Players need the same protocol version (src/protocol.hpp)
# to play together; the mod version itself may differ.
project(hyrule_hide_and_seek VERSION 0.4.1 LANGUAGES CXX)

# The Dusklight version to build against: a release tag or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "v2.0.3")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR=/path/to/dusklight to build against an existing checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

# The relay every player connects to. Change it after deploying server/ (see server/README.md),
# or override per player with the "server" setting in the mod's Settings tab.
set(HS_DEFAULT_SERVER "wss://hyrule-hide-and-seek.jhackerr.workers.dev" CACHE STRING "Default relay URL")

configure_file(mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY NEWLINE_STYLE LF)

set(hs_res "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/icon.png")
    set(hs_res RES_DIR res)
endif ()
add_mod(hyrule_hide_and_seek
        FEATURES game fmt
        SOURCES
            src/mod.cpp
            src/net.cpp
            src/match.cpp
            src/maps.cpp
            src/props.cpp
            src/local.cpp
            src/linkkit.cpp
            src/recolor.cpp
            src/puppet.cpp
            src/hud.cpp
            src/ui.cpp
            src/settings.cpp
            src/game_mode.cpp
            src/arena.cpp
            src/treasure.cpp
        MOD_JSON "${CMAKE_CURRENT_BINARY_DIR}/mod.json"
        ${hs_res}
)
target_compile_definitions(hyrule_hide_and_seek PRIVATE
        HS_DEFAULT_SERVER="${HS_DEFAULT_SERVER}"
        HS_MOD_VERSION="${PROJECT_VERSION}")

# Opt-in game integration test; never enabled in distributed bundles.
option(HS_STOCK_RENDER_TEST "Exercise stock Dusklight's retained draw lists in the game" OFF)
if (HS_STOCK_RENDER_TEST)
    target_sources(hyrule_hide_and_seek PRIVATE tests/stock_render_driver.cpp)
    target_include_directories(hyrule_hide_and_seek PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(hyrule_hide_and_seek PRIVATE HS_STOCK_RENDER_TEST=1)
endif ()

# Local in-game test harness (tools/lab); never included in the release build.
option(HS_LAB "Enable the local in-game test harness" OFF)
if (HS_LAB)
    target_sources(hyrule_hide_and_seek PRIVATE tools/lab/lab.cpp)
    target_include_directories(hyrule_hide_and_seek PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(hyrule_hide_and_seek PRIVATE HS_LAB=1)
endif ()

# Local filming tools; never included in the release build.
option(HS_SHOWCASE "Enable the local bot-session filming director" OFF)
if (HS_SHOWCASE)
    target_sources(hyrule_hide_and_seek PRIVATE tools/showcase/director.cpp)
    target_include_directories(hyrule_hide_and_seek PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(hyrule_hide_and_seek PRIVATE HS_SHOWCASE=1)
endif ()
