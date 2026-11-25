# Copyright 2026 davenonymous
#
# Static, position-independent release libraries for the x86 SteamWorks
# stand-in extension. vcpkg's Linux toolchain adds -m32 for x86 on an x86_64
# host.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "-fPIC -fvisibility=hidden")
set(VCPKG_CXX_FLAGS "-fPIC -fvisibility=hidden")
