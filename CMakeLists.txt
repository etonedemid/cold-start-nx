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
    target_compile_definitions(cold_start PRIVATE PLATFORM_PC)

    if(ENET_FOUND)
        target_include_directories(cold_start PRIVATE ${ENET_INCLUDE_DIRS})
        target_link_libraries(cold_start ${ENET_LIBRARIES})
        target_compile_definitions(cold_start PRIVATE HAS_ENET=1)
        message(STATUS "ENet found — multiplayer enabled")
    else()
        target_compile_definitions(cold_start PRIVATE HAS_ENET=0)
        message(STATUS "ENet NOT found — multiplayer disabled (stub)")
    endif()

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
