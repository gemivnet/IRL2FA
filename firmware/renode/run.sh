#!/usr/bin/env bash
# Builds the firmware (the release image: the tests run exactly what ships)
# and runs the Renode tests in Docker.
#
#   firmware/renode/run.sh                 # all tests
#   firmware/renode/run.sh --include tag   # extra renode-test arguments
#   SKIP_BUILD=1 firmware/renode/run.sh    # reuse firmware/build/stm32u0
#
# Results, logs and screen dumps (PNG/PBM): firmware/build/renode/
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$FW/.." && pwd)"
IMAGE="${RENODE_IMAGE:-antmicro/renode:1.16.1}"
OUT="$FW/build/renode"

if [[ -z "${SKIP_BUILD:-}" ]]; then
    BUILD_DIR="$FW/build/stm32u0" "$FW/port/stm32u0/build.sh"
fi
mkdir -p "$OUT"

docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$REPO:$REPO" -w "$REPO" "$IMAGE" \
    renode-test --results-dir "$OUT" --show-log "$@" "$HERE/irl2fa.robot" "$HERE/power.robot"
