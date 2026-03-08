# ─── COLD START — Android Native Build ───────────────────────────────────────
#
# This CMakeLists.txt is invoked by Gradle's externalNativeBuild.
# It builds SDL2 + extensions from source, then compiles the game as libmain.so.
#
# Prerequisites: run android/setup_sdl.sh to download SDL2 sources.
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.14)
project(cold_start_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── SDL2 from source ────────────────────────────────────────────────────────

set(SDL2_SOURCE_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/SDL2"       CACHE PATH "")
set(SDL2_IMAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL2_image" CACHE PATH "")
set(SDL2_TTF_SOURCE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf"   CACHE PATH "")
set(SDL2_MIXER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL2_mixer" CACHE PATH "")

add_subdirectory(${SDL2_SOURCE_DIR}       SDL2       EXCLUDE_FROM_ALL)
add_subdirectory(${SDL2_IMAGE_SOURCE_DIR} SDL2_image EXCLUDE_FROM_ALL)
add_subdirectory(${SDL2_TTF_SOURCE_DIR}   SDL2_ttf   EXCLUDE_FROM_ALL)
add_subdirectory(${SDL2_MIXER_SOURCE_DIR} SDL2_mixer EXCLUDE_FROM_ALL)

# ── Game sources ────────────────────────────────────────────────────────────

set(GAME_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../source")

file(GLOB GAME_SOURCES  ${GAME_SRC}/*.cpp)
file(GLOB ENET_SOURCES  ${GAME_SRC}/enet/*.c)
list(FILTER ENET_SOURCES EXCLUDE REGEX "win32\\.c$")

add_library(main SHARED ${GAME_SOURCES} ${ENET_SOURCES})

target_include_directories(main PRIVATE
    ${GAME_SRC}
    ${GAME_SRC}/enet/include)

target_link_libraries(main PRIVATE
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer)

target_compile_definitions(main PRIVATE
    PLATFORM_ANDROID
    HAS_ENET=1
    HAS_UPNP=0)
