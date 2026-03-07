# ─── COLD START — Cross-platform CMake build ────────────────────────────────
# Usage:
#   PC:      mkdir build-pc && cd build-pc && cmake .. && make -j$(nproc)
#   Android: Use with SDL2's android-project template (see README)
#   Switch:  Use the devkitPro Makefile (make -j4)
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.14)
project(cold_start LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ──
file(GLOB SOURCES source/*.cpp)
file(GLOB HEADERS source/*.h)

add_executable(cold_start ${SOURCES} ${HEADERS})

# ── Platform detection ──
if(ANDROID)
    # Android NDK build — SDL2 is provided by the android-project template
    find_library(SDL2 SDL2)
    find_library(SDL2_IMAGE SDL2_image)
    find_library(SDL2_TTF SDL2_ttf)
    find_library(SDL2_MIXER SDL2_mixer)
    target_link_libraries(cold_start SDL2 SDL2_image SDL2_ttf SDL2_mixer)
    target_compile_definitions(cold_start PRIVATE PLATFORM_ANDROID)
else()
    # PC (Linux / macOS / Windows)
    if(WIN32)
        # Windows — use vcpkg CMake config packages (MSVC) or MinGW system libs
        find_package(SDL2       CONFIG REQUIRED)
        find_package(SDL2_image CONFIG REQUIRED)
        find_package(SDL2_ttf   CONFIG REQUIRED)
        find_package(SDL2_mixer CONFIG REQUIRED)

        if(MINGW)
            # MinGW: link everything statically → single .exe
            # When cross-compiling from Linux the static webp lives in a separate
            # prefix; add it first so the linker picks .a before .dll.a.
            if(CMAKE_CROSSCOMPILING AND EXISTS /usr/x86_64-w64-mingw32/static/lib)
                target_link_directories(cold_start BEFORE PRIVATE
                    /usr/x86_64-w64-mingw32/static/lib
                )
            endif()
            target_link_libraries(cold_start PRIVATE
                SDL2::SDL2main
                SDL2::SDL2-static
                SDL2_image::SDL2_image-static
                SDL2_ttf::SDL2_ttf-static
                SDL2_mixer::SDL2_mixer-static
            )
            # Embed the GCC/C++ runtimes and winpthreads so no DLLs are needed
            target_link_options(cold_start PRIVATE
                -static-libgcc
                -static-libstdc++
                -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
                -mwindows
            )
        else()
            # vcpkg / MSVC: shared DLLs
            target_link_libraries(cold_start PRIVATE
                SDL2::SDL2main
                SDL2::SDL2
                SDL2_image::SDL2_image
                SDL2_ttf::SDL2_ttf
                SDL2_mixer::SDL2_mixer
            )
        endif()

        # ENet (optional — multiplayer support)
        find_package(unofficial-enet CONFIG)
        if(unofficial-enet_FOUND)
            target_link_libraries(cold_start PRIVATE unofficial::enet::enet)
            target_compile_definitions(cold_start PRIVATE HAS_ENET=1)
            message(STATUS "ENet found — multiplayer enabled")
        else()
            target_compile_definitions(cold_start PRIVATE HAS_ENET=0)
            message(STATUS "ENet NOT found — multiplayer disabled (stub)")
        endif()

        # Winsock2 — required for gethostname/getaddrinfo in getLocalIP()
        target_link_libraries(cold_start PRIVATE ws2_32)
    else()
        # Linux / macOS — use pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2       REQUIRED sdl2)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        pkg_check_modules(SDL2_TTF   REQUIRED SDL2_ttf)
        pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

        # ENet (optional — multiplayer support)
        pkg_check_modules(ENET libenet)

        target_include_directories(cold_start PRIVATE
            ${SDL2_INCLUDE_DIRS}
            ${SDL2_IMAGE_INCLUDE_DIRS}
            ${SDL2_TTF_INCLUDE_DIRS}
            ${SDL2_MIXER_INCLUDE_DIRS}
        )
        target_link_libraries(cold_start
            ${SDL2_LIBRARIES}
            ${SDL2_IMAGE_LIBRARIES}
            ${SDL2_TTF_LIBRARIES}
            ${SDL2_MIXER_LIBRARIES}
        )

        if(ENET_FOUND)
            target_include_directories(cold_start PRIVATE ${ENET_INCLUDE_DIRS})
            target_link_libraries(cold_start ${ENET_LIBRARIES})
            target_compile_definitions(cold_start PRIVATE HAS_ENET=1)
            message(STATUS "ENet found — multiplayer enabled")
        else()
            target_compile_definitions(cold_start PRIVATE HAS_ENET=0)
            message(STATUS "ENet NOT found — multiplayer disabled (stub)")
        endif()
    endif()

    # miniupnpc (optional — automatic UPnP port forwarding when hosting)
    if(NOT WIN32)
        pkg_check_modules(MINIUPNPC miniupnpc)
    endif()
    if(MINIUPNPC_FOUND)
        target_include_directories(cold_start PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(cold_start ${MINIUPNPC_LIBRARIES})
        target_compile_definitions(cold_start PRIVATE HAS_UPNP=1)
        message(STATUS "miniupnpc found — UPnP port forwarding enabled")
    else()
        target_compile_definitions(cold_start PRIVATE HAS_UPNP=0)
        message(STATUS "miniupnpc NOT found — UPnP disabled")
    endif()

    target_compile_definitions(cold_start PRIVATE PLATFORM_PC)

    # Copy romfs to build dir as "romfs/" so asset paths work during development
    add_custom_command(TARGET cold_start POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/romfs
            $<TARGET_FILE_DIR:cold_start>/romfs
    )

    # ── dist target — self-contained bundle ─────────────────────────────────
    # Usage:  make dist
    # Output: build-pc/dist/  containing the exe, romfs/, libs/, and run.sh
    configure_file(
        ${CMAKE_SOURCE_DIR}/bundle.sh.in
        ${CMAKE_BINARY_DIR}/bundle.sh
        @ONLY
    )
    file(CHMOD ${CMAKE_BINARY_DIR}/bundle.sh PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

    add_custom_target(dist
        DEPENDS cold_start
        COMMAND ${CMAKE_BINARY_DIR}/bundle.sh
        COMMENT "Building distributable bundle -> dist/"
    )
endif()

target_include_directories(cold_start PRIVATE source)
