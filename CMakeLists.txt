# ─── COLD START - Cross-platform CMake Build ────────────────────────────────
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
    # Headless dedicated server - only the networking + minimal game logic
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
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS source/*.cpp)
    list(FILTER SOURCES EXCLUDE REGEX "server_main\\.cpp$")
    file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS source/*.h)
endif()

if(WIN32)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()
add_executable(cold_start ${SOURCES} ${HEADERS})
target_include_directories(cold_start PRIVATE source)

# ── Platform: Windows (MinGW / MSVC) ────────────────────────────────────────

if(WIN32)
    find_package(SDL2       CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_ttf   CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    find_package(CURL REQUIRED)
    target_link_libraries(cold_start PRIVATE CURL::libcurl)
    target_compile_definitions(cold_start PRIVATE HAS_CURL=1)
    message(STATUS "libcurl: found and linked")

    if(MINGW)
        # Static linking - produces a single portable .exe
        if(CMAKE_CROSSCOMPILING AND EXISTS /usr/x86_64-w64-mingw32/static/lib)
            target_link_directories(cold_start BEFORE PRIVATE
                /usr/x86_64-w64-mingw32/static/lib)
        endif()

        target_link_libraries(cold_start PRIVATE
            SDL2::SDL2main SDL2::SDL2-static
            SDL2_image::SDL2_image-static
            SDL2_ttf::SDL2_ttf-static
            SDL2_mixer::SDL2_mixer-static)

        target_compile_definitions(cold_start PRIVATE _GLIBCXX_USE_DEPRECATED=1)
        target_compile_options(cold_start PRIVATE -Wno-deprecated-declarations)

        # libwinpthread.a ships a version.o with its own VERSIONINFO resource.
        # --whole-archive forces it in and clashes with the one in app.rc.
        # Strip the .rsrc section from a local copy and link that instead.
        find_program(OBJCOPY_EXE NAMES objcopy x86_64-w64-mingw32-objcopy)
        if(OBJCOPY_EXE)
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} --print-file-name=libwinpthread.a
                OUTPUT_VARIABLE WINPTHREAD_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
            set(WINPTHREAD_STRIPPED "${CMAKE_BINARY_DIR}/libwinpthread_nores.a")
            find_program(AR_EXE NAMES ar x86_64-w64-mingw32-ar llvm-ar)
            add_custom_command(
                OUTPUT  "${WINPTHREAD_STRIPPED}"
                COMMAND ${CMAKE_COMMAND} -E copy "${WINPTHREAD_LIB}" "${WINPTHREAD_STRIPPED}"
                COMMAND "${AR_EXE}" d "${WINPTHREAD_STRIPPED}" version.o
                DEPENDS "${WINPTHREAD_LIB}"
                COMMENT "Removing version.o from libwinpthread.a to avoid VERSIONINFO clash"
            )
            add_custom_target(winpthread_nores DEPENDS "${WINPTHREAD_STRIPPED}")
            add_dependencies(cold_start winpthread_nores)
            target_link_options(cold_start PRIVATE
                -static-libgcc -static-libstdc++
                -Wl,-Bstatic,--whole-archive "${WINPTHREAD_STRIPPED}" -Wl,--no-whole-archive,-Bdynamic
                -mwindows)
        else()
            target_link_options(cold_start PRIVATE
                -static-libgcc -static-libstdc++
                -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
                -mwindows)
        endif()

    else()
        # MSVC / vcpkg - dynamic linking
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

    # miniupnpc (optional - UPnP port forwarding for hosting)
    find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h
        HINTS ${MINIUPNPC_INCLUDE_DIR} "C:/msys64/mingw64/include" "C:/msys2/mingw64/include" ENV MINGW_PREFIX
        NO_DEFAULT_PATH)
    find_library(MINIUPNPC_LIB NAMES miniupnpc libminiupnpc
        HINTS ${MINIUPNPC_LIBRARY} "C:/msys64/mingw64/lib" "C:/msys2/mingw64/lib" ENV MINGW_PREFIX
        NO_DEFAULT_PATH)
    if(MINIUPNPC_LIBRARY AND NOT MINIUPNPC_LIB)
        set(MINIUPNPC_LIB ${MINIUPNPC_LIBRARY})
    endif()
    if(MINIUPNPC_INCLUDE_DIR AND MINIUPNPC_LIB)
        target_include_directories(cold_start PRIVATE ${MINIUPNPC_INCLUDE_DIR})
        # miniupnpc also needs iphlpapi (GetBestInterface, GetAdaptersAddresses)
        # and additional ws2_32 symbols (inet_addr, sendto, getnameinfo)
        target_link_libraries(cold_start PRIVATE ${MINIUPNPC_LIB} iphlpapi)
        # MINIUPNP_STATICLIB (no C) suppresses __declspec(dllimport) in the headers
        target_compile_definitions(cold_start PRIVATE HAS_ENET=1 HAS_UPNP=1 PLATFORM_PC MINIUPNP_STATICLIB)
        message(STATUS "miniupnpc: found - UPnP enabled")
    else()
        target_compile_definitions(cold_start PRIVATE HAS_ENET=1 HAS_UPNP=0 PLATFORM_PC)
        message(STATUS "miniupnpc: not found - UPnP disabled")
    endif()

# ── Platform: Linux / macOS ─────────────────────────────────────────────────

else()
    find_package(PkgConfig REQUIRED)

    if(NOT SERVER_ONLY)

        pkg_check_modules(SDL2       REQUIRED sdl2)
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        pkg_check_modules(SDL2_TTF   REQUIRED SDL2_ttf)
        pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(CURL REQUIRED libcurl)
        target_link_libraries(cold_start PRIVATE ${CURL_LIBRARIES})
        target_compile_definitions(cold_start PRIVATE HAS_CURL=1)

        target_include_directories(cold_start PRIVATE
            ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS}
            ${SDL2_TTF_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})

        target_link_libraries(cold_start PRIVATE
            ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES}
            ${SDL2_TTF_LIBRARIES} ${SDL2_MIXER_LIBRARIES})
    else()
        target_compile_definitions(cold_start PRIVATE DEDICATED_SERVER=1)
        message(STATUS "SERVER_ONLY build - SDL omitted")
    endif()

    # ENet: try system package, fall back to bundled sources
    pkg_check_modules(ENET libenet)
    if(ENET_FOUND)
        target_include_directories(cold_start PRIVATE ${ENET_INCLUDE_DIRS})
        target_link_libraries(cold_start PRIVATE ${ENET_LIBRARIES})
        message(STATUS "ENet: system package found")
    else()
        file(GLOB _ENET_C source/enet/*.c)
        list(FILTER _ENET_C EXCLUDE REGEX "win32\\.c$")
        target_sources(cold_start PRIVATE ${_ENET_C})
        target_include_directories(cold_start PRIVATE source/enet/include)
        message(STATUS "ENet: using bundled sources")
    endif()
    target_compile_definitions(cold_start PRIVATE HAS_ENET=1)

    # miniupnpc (optional - UPnP port forwarding for hosting, not needed server-side)
    if(NOT SERVER_ONLY)
        pkg_check_modules(MINIUPNPC miniupnpc)
        if(MINIUPNPC_FOUND)
            target_include_directories(cold_start PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
            target_link_libraries(cold_start PRIVATE ${MINIUPNPC_LIBRARIES})
            target_compile_definitions(cold_start PRIVATE HAS_UPNP=1)
            message(STATUS "miniupnpc: found - UPnP enabled")
        else()
            target_compile_definitions(cold_start PRIVATE HAS_UPNP=0)
            message(STATUS "miniupnpc: not found - UPnP disabled")
        endif()
    else()
        target_compile_definitions(cold_start PRIVATE HAS_UPNP=0)
        message(STATUS "miniupnpc: skipped (SERVER_ONLY)")
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
        COMMENT "Packaging distributable bundle -> dist/")
endif()
