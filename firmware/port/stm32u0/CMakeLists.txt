# IRL2FA firmware for the STM32U083KC. Build with ./build.sh (Docker) or:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake && cmake --build build
# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(irl2fa C)

option(IRL2FA_DEBUG_HOOK "RAM debug block read by the Renode tests (no secrets)" ON)
option(EPD_ROTATE_180 "Panel mounted the other way round" OFF)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_executable(irl2fa.elf
    startup_stm32u083.c
    main.c
    hal_stm32u0.c
    epd_ssd1680.c
    rv3032.c
    ${CORE}/app.c ${CORE}/base32.c ${CORE}/buttons.c ${CORE}/fonts.c ${CORE}/gfx.c ${CORE}/hash.c
    ${CORE}/pairing.c ${CORE}/store.c ${CORE}/timeutil.c ${CORE}/totp.c
    ${CORE}/wordlist.c ${CORE}/words.c
    ${CORE}/third_party/monocypher/monocypher.c)

target_include_directories(irl2fa.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CORE}
    ${VENDOR}/cmsis_core ${VENDOR}/cmsis_device_u0 ${VENDOR}/stm32u0xx_ll)

set(MCU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)
target_compile_definitions(irl2fa.elf PRIVATE STM32U083xx
    IRL2FA_DEBUG_HOOK=$<BOOL:${IRL2FA_DEBUG_HOOK}>
    $<$<BOOL:${EPD_ROTATE_180}>:EPD_ROTATE_180>)
target_compile_options(irl2fa.elf PRIVATE ${MCU_FLAGS} -std=c11 -Os -g3 -flto=auto
    -ffunction-sections -fdata-sections -fno-common
    -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter)
# Monocypher is third-party: compile it with the same flags but do not fail
# the build on its warnings.
set_source_files_properties(${CORE}/third_party/monocypher/monocypher.c
    PROPERTIES COMPILE_OPTIONS "-Wno-error")
target_link_options(irl2fa.elf PRIVATE ${MCU_FLAGS} -Os -flto=auto
    -T${CMAKE_CURRENT_SOURCE_DIR}/stm32u083kc.ld -nostartfiles
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections -Wl,-Map=irl2fa.map -Wl,--print-memory-usage)
set_target_properties(irl2fa.elf PROPERTIES LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32u083kc.ld)

add_custom_command(TARGET irl2fa.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary irl2fa.elf irl2fa.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex irl2fa.elf irl2fa.hex
    COMMAND ${CMAKE_SIZE} -A -x irl2fa.elf > irl2fa.size.txt
    COMMAND ${CMAKE_SIZE} -B irl2fa.elf
    BYPRODUCTS irl2fa.bin irl2fa.hex irl2fa.size.txt irl2fa.map)
