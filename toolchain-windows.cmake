# toolchain-windows.cmake — MinGW-w64 cross-compilation to Windows x86_64
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain-windows.cmake ..

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER    x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER  x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER   x86_64-w64-mingw32-windres)

set(MINGW_PREFIX /usr/x86_64-w64-mingw32)

# Put the static-only dir first so find_library() picks .a over .dll.a
set(CMAKE_FIND_ROOT_PATH ${MINGW_PREFIX}/static ${MINGW_PREFIX})
set(CMAKE_PREFIX_PATH    ${MINGW_PREFIX}/static ${MINGW_PREFIX})

# Search for headers/libs only in the cross prefix; search host for programs
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
