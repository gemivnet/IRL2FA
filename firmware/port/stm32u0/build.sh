#!/usr/bin/env bash
# Cross-builds the STM32U083 firmware in Docker (Debian bookworm's
# gcc-arm-none-eabi 12.2 + newlib-nano) and prints the size report.
#
#   firmware/port/stm32u0/build.sh                 # release build -> firmware/build/stm32u0/
#   firmware/port/stm32u0/build.sh -DEPD_ROTATE_180=ON       # extra CMake args
#   BUILD_DIR=/abs/dir firmware/port/stm32u0/build.sh ...
#   NO_DOCKER=1 firmware/port/stm32u0/build.sh     # use the host toolchain
#
# Outputs: $BUILD_DIR/irl2fa.{elf,bin,hex,map,size.txt}
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$(cd "$HERE/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$FW/build/stm32u0}"
IMAGE="irl2fa-arm-gcc:bookworm-1"

inner() {
    cd "$HERE"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake "$@" >/dev/null
    cmake --build "$BUILD_DIR" -- --no-print-directory
    python3 "$HERE/size_report.py" "$BUILD_DIR/irl2fa.elf" | tee "$BUILD_DIR/size-report.txt"
}

if [[ "${1:-}" == "--inner" ]]; then
    shift
    inner "$@"
    exit 0
fi
if [[ -n "${NO_DOCKER:-}" ]]; then
    inner "$@"
    exit 0
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" - <<'DOCKERFILE'
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc-arm-none-eabi libnewlib-arm-none-eabi \
      binutils-arm-none-eabi cmake make python3 \
 && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

docker run --rm -u "$(id -u):$(id -g)" -e BUILD_DIR="$BUILD_DIR" -v "$FW:$FW" -w "$HERE" "$IMAGE" \
    bash "$HERE/build.sh" --inner "$@"
