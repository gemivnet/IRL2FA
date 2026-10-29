#!/usr/bin/env bash
# Builds the simulator, serves it locally and captures the docs screenshots in
# the Playwright Docker image. Usage: sim/tools/screenshots.sh [URL]
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../.."
IMAGE=mcr.microsoft.com/playwright:v1.49.1-noble
URL=${1:-}
if [ -z "$URL" ]; then
  [ -f sim/dist/irl2fa.wasm ] || sim/build.sh
  URL=http://127.0.0.1:8123/
  SERVE="python3 -m http.server 8123 --bind 127.0.0.1 --directory sim/dist >/dev/null 2>&1 &"
else
  SERVE=true
fi
docker run --rm --network host -u "$(id -u):$(id -g)" -e HOME=/tmp -e SIM_URL="$URL" \
  -v "$PWD":/w -w /w "$IMAGE" bash -c "
    $SERVE
    cd /tmp && npm init -y >/dev/null && npm i -s playwright@1.49.1 >/dev/null && cd /w &&
    NODE_PATH=/tmp/node_modules node --input-type=module -e \"\$(cat sim/tools/screenshots.mjs | sed 's#from .playwright.;#from \\\"/tmp/node_modules/playwright/index.mjs\\\";#')\""
