set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(tools /opt/x-tools/rasfire)

set(CMAKE_C_COMPILER ${tools}/bin/rasfire-gcc)
set(CMAKE_SYSROOT ${tools}/aarch64-linux-gnu/sysroot)

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)
set(CPACK_PACKAGE_NAME "libwaveform")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})

set(CMAKE_C_FLAGS "-static-libstdc++" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++" CACHE STRING "" FORCE)

set(CMAKE_CROSSCOMPILING_EMULATOR "/usr/bin/qemu-aarch64-static;-L;/usr/aarch64-linux-gnu/" CACHE FILEPATH "Path to the ARM Emulator")