# Toolchain file for cross-compiling to Rockchip RK3506 (ARM)
# Usage:
#   cmake -S . -B build-rk3506 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake \
#     -DCMAKE_SYSROOT=/path/to/rk3506/sysroot \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DBUILD_TESTING=OFF

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Change this if your SDK uses another prefix.
set(TOOLCHAIN_PREFIX arm-linux-gnueabihf)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}-gcc)

# Optional CPU tuning. Keep empty by default to avoid mismatches across RK3506 SDK variants.
# Example:
# set(RK3506_CPU_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(RK3506_CPU_FLAGS "" CACHE STRING "Extra CPU flags for RK3506")

if(RK3506_CPU_FLAGS)
  set(CMAKE_C_FLAGS_INIT "${RK3506_CPU_FLAGS}")
  set(CMAKE_CXX_FLAGS_INIT "${RK3506_CPU_FLAGS}")
endif()

# If your cross SDK provides a sysroot, pass it in with -DCMAKE_SYSROOT=/path/to/sysroot.
# set(CMAKE_SYSROOT /path/to/sysroot)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
