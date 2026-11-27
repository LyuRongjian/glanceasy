set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(RV1106_TOOLCHAIN
    /home/lrj/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf)

set(CMAKE_C_COMPILER
    ${RV1106_TOOLCHAIN}/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER
    ${RV1106_TOOLCHAIN}/bin/arm-rockchip830-linux-uclibcgnueabihf-g++)
set(CMAKE_SYSROOT
    ${RV1106_TOOLCHAIN}/arm-rockchip830-linux-uclibcgnueabihf/sysroot)

set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
