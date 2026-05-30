# ─── COLD START — Android Native Build ───────────────────────────────────────

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

# ── SDL2 include shim ───────────────────────────────────────────────────────
# Game source uses <SDL2/SDL.h> but SDL built from source exposes headers flat.
# Create ${build}/sdl_shim/SDL2/ and symlink each public header into it so
# #include <SDL2/SDL*.h> resolves correctly.

set(_shim_dir "${CMAKE_BINARY_DIR}/sdl_shim/SDL2")
file(MAKE_DIRECTORY "${_shim_dir}")

# Helper: symlink all *.h files from a given directory into the shim
macro(_link_sdl_headers _src_dir)
    file(GLOB _hdrs "${_src_dir}/*.h")
    foreach(_h ${_hdrs})
        get_filename_component(_n "${_h}" NAME)
        if(NOT EXISTS "${_shim_dir}/${_n}")
            execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${_h}" "${_shim_dir}/${_n}")
        endif()
    endforeach()
endmacro()

_link_sdl_headers("${SDL2_SOURCE_DIR}/include")    # SDL.h, SDL_events.h, ...
_link_sdl_headers("${SDL2_IMAGE_SOURCE_DIR}/include")  # SDL_image.h
_link_sdl_headers("${SDL2_TTF_SOURCE_DIR}")            # SDL_ttf.h (at root)
_link_sdl_headers("${SDL2_MIXER_SOURCE_DIR}/include")  # SDL_mixer.h

# ── Game sources ────────────────────────────────────────────────────────────

set(GAME_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../source")

file(GLOB GAME_SOURCES ${GAME_SRC}/*.cpp)
list(FILTER GAME_SOURCES EXCLUDE REGEX "server_main\\.cpp$")
file(GLOB ENET_SOURCES ${GAME_SRC}/enet/*.c)
list(FILTER ENET_SOURCES EXCLUDE REGEX "win32\\.c$")

add_library(main SHARED ${GAME_SOURCES} ${ENET_SOURCES})

target_include_directories(main PRIVATE
    "${GAME_SRC}"
    "${GAME_SRC}/enet/include"
    "${CMAKE_BINARY_DIR}/sdl_shim")    # makes <SDL2/SDL.h> work

target_link_libraries(main PRIVATE
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    android log)

target_compile_definitions(main PRIVATE
    __ANDROID__
    PLATFORM_ANDROID
    HAS_ENET=1
    HAS_UPNP=0
    HAS_SOCKLEN_T=1
    HAS_FCNTL=1
    HAS_POLL=1
    HAS_GETADDRINFO=1
    HAS_GETNAMEINFO=1
    HAS_INET_PTON=1
    HAS_INET_NTOP=1)
