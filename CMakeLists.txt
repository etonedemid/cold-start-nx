# ─── COLD START — Cross-platform CMake Build ────────────────────────────────
#
# Targets:
#   Linux:   cmake -B build-pc && cmake --build build-pc -j$(nproc)
#   Server:  cmake -B build-server -DSERVER_ONLY=ON && cmake --build build-server -j$(nproc)
#   Windows: cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=toolchain-windows.cmake
#            cmake --build build-win -j$(nproc)
#   MSYS2:   cmake -B build-win -G Ninja && cmake --build build-win
#   Android: See android/ directory
#   Switch:  Use the devkitPro Makefile (make -j4)
#
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.14)
project(cold_start LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────

option(SERVER_ONLY "Build headless dedicated server without SDL (Linux only)" OFF)

# ── Source files ─────────────────────────────────────────────────────────────

if(SERVER_ONLY)
    # Headless dedicated server — only the networking + minimal game logic
    set(SOURCES
        source/server_main.cpp
        source/network.cpp
        source/gamemode.cpp
        source/mapformat.cpp
        source/mappack.cpp
    )
    file(GLOB HEADERS source/network.h source/gamemode.h source/mapformat.h
                      source/mappack.h source/vec2.h source/constants.h)
else()
    file(GLOB SOURCES source/*.cpp)
    # Exclude the server-only entry point from regular builds
    list(FILTER SOURCES EXCLUDE REGEX "server_main\\.cpp$")
    file(GLOB HEADERS source/*.h)
endif()

add_executable(cold_start ${SOURCES} ${HEADERS})
target_include_directories(cold_start PRIVATE source)

# ── Platform: Windows (MinGW / MSVC) ────────────────────────────────────────

if(WIN32)
    find_package(SDL2       CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_ttf   CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)

    if(MINGW)
        # Static linking — produces a single portable .exe
        if(CMAKE_CROSSCOMPILING AND EXISTS /usr/x86_64-w64-mingw32/static/lib)
            target_link_directories(cold_start BEFORE PRIVATE
                /usr/x86_64-w64-mingw32/static/lib)
        endif()

        target_link_libraries(cold_start PRIVATE
            SDL2::SDL2main SDL2::SDL2-static
            SDL2_image::SDL2_image-static
            SDL2_ttf::SDL2_ttf-static
            SDL2_mixer::SDL2_mixer-static)

        target_link_options(cold_start PRIVATE
            -static-libgcc -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
            -mwindows)
    else()
        # MSVC / vcpkg — dynamic linking
        target_link_libraries(cold_start PRIVATE
            SDL2::SDL2main SDL2::SDL2
            SDL2_image::SDL2_image
            SDL2_ttf::SDL2_ttf
            SDL2_mixer::SDL2_mixer)
    endif()

    # ENet: try vcpkg first, fall back to bundled sources
    find_package(unofficial-enet CONFIG QUIET)
    if(unofficial-enet_FOUND)
        target_link_libraries(cold_start PRIVATE unofficial::enet::enet)
        message(STATUS "ENet: vcpkg package found")
    else()
        file(GLOB _ENET_C source/enet/*.c)
        list(FILTER _ENET_C EXCLUDE REGEX "unix\\.c$")
        target_sources(cold_start PRIVATE ${_ENET_C})
        target_include_directories(cold_start PRIVATE source/enet/include)
        target_link_libraries(cold_start PRIVATE winmm)
        message(STATUS "ENet: using bundled sources")
    endif()

    target_link_libraries(cold_start PRIVATE ws2_32)
    target_compile_definitions(cold_start PRIVATE HAS_ENET=1 HAS_UPNP=0 PLATFORM_PC)

# ── Platform: Linux / macOS ─────────────────────────────────────────────────

else()
    find_package(PkgConfig REQUIRED)

    if(NOT SERVER_ONLY)
        pkg_check_modules(SDL2       REQUIRED sdl2)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        pkg_check_modules(SDL2_TTF   REQUIRED SDL2_ttf)
        pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

        target_include_directories(cold_start PRIVATE
            ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS}
            ${SDL2_TTF_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})

        target_link_libraries(cold_start
            ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES}
            ${SDL2_TTF_LIBRARIES} ${SDL2_MIXER_LIBRARIES})
    else()
        target_compile_definitions(cold_start PRIVATE DEDICATED_SERVER=1)
        message(STATUS "SERVER_ONLY build — SDL omitted")
    endif()

    # ENet: try system package, fall back to bundled sources
    pkg_check_modules(ENET libenet)
    if(ENET_FOUND)
        target_include_directories(cold_start PRIVATE ${ENET_INCLUDE_DIRS})
        target_link_libraries(cold_start ${ENET_LIBRARIES})
        message(STATUS "ENet: system package found")
    else()
        file(GLOB _ENET_C source/enet/*.c)
        list(FILTER _ENET_C EXCLUDE REGEX "win32\\.c$")
        target_sources(cold_start PRIVATE ${_ENET_C})
        target_include_directories(cold_start PRIVATE source/enet/include)
        message(STATUS "ENet: using bundled sources")
    endif()
    target_compile_definitions(cold_start PRIVATE HAS_ENET=1)

    # miniupnpc (optional — UPnP port forwarding for hosting)
    pkg_check_modules(MINIUPNPC miniupnpc)
    if(MINIUPNPC_FOUND)
        target_include_directories(cold_start PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(cold_start ${MINIUPNPC_LIBRARIES})
        target_compile_definitions(cold_start PRIVATE HAS_UPNP=1)
        message(STATUS "miniupnpc: found — UPnP enabled")
    else()
        target_compile_definitions(cold_start PRIVATE HAS_UPNP=0)
        message(STATUS "miniupnpc: not found — UPnP disabled")
    endif()

    target_compile_definitions(cold_start PRIVATE PLATFORM_PC)
endif()

# ── Post-build: mirror romfs into build directory ────────────────────────────

if(NOT SERVER_ONLY)
    add_custom_command(TARGET cold_start POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/romfs $<TARGET_FILE_DIR:cold_start>/romfs)
endif()

# ── dist target: self-contained Linux bundle ─────────────────────────────────

if(UNIX AND NOT APPLE AND NOT SERVER_ONLY)
    configure_file(
        ${CMAKE_SOURCE_DIR}/bundle.sh.in
        ${CMAKE_BINARY_DIR}/bundle.sh @ONLY)
    file(CHMOD ${CMAKE_BINARY_DIR}/bundle.sh PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    add_custom_target(dist
        DEPENDS cold_start
        COMMAND ${CMAKE_BINARY_DIR}/bundle.sh
        COMMENT "Packaging distributable bundle → dist/")
endif()
