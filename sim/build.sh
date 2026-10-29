#!/usr/bin/env bash
# Builds the browser simulator into sim/dist/:
#   the firmware core + virtual board + sim_main.c compiled to WebAssembly,
#   plus the static page from sim/web/.
#
# Uses emcc directly if it is on PATH (e.g. in CI with setup-emsdk), otherwise
# the emscripten/emsdk:3.1.74 docker image.
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

SIM="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SIM/.." && pwd)"
DIST="$SIM/dist"
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.74}"

CORE="firmware/core/hash.c firmware/core/base32.c firmware/core/totp.c
      firmware/core/words.c firmware/core/wordlist.c firmware/core/pairing.c
      firmware/core/store.c firmware/core/gfx.c firmware/core/fonts.c
      firmware/core/timeutil.c firmware/core/app.c
      firmware/core/third_party/monocypher/monocypher.c
      firmware/vhal/vhal.c sim/sim_main.c"

EXPORTS='["_sim_reset","_sim_init","_sim_poll","_sim_wake","_sim_sleeping",
"_sim_set_buttons","_sim_set_battery_mv","_sim_advance","_sim_set_unix",
"_sim_set_unix_ms","_sim_get_unix","_sim_millis","_sim_framebuffer",
"_sim_refreshes","_sim_display_w","_sim_display_h","_sim_display_stride",
"_sim_screen_name","_sim_io_buf","_sim_io_cap","_sim_ir_on","_sim_ir_take",
"_sim_ir_rx","_sim_str_buf","_sim_str_cap","_sim_unlocked","_sim_pin_set",
"_sim_add_totp","_sim_refresh_ui","_sim_count","_sim_totp_code",
"_sim_record_name","_sim_contact_words","_sim_word","_sim_probe_text",
"_sim_energy_reset","_sim_energy_awake_ms","_sim_energy_asleep_ms",
"_sim_energy_ir_ms","_sim_energy_full","_sim_energy_fast","_sim_energy_wakes"]'
EXPORTS="$(echo "$EXPORTS" | tr -d '\n ')"

EMFLAGS=(
  -std=c11 -O2 -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
  -sEXPORT_NAME=createIRL2FA
  -sEXPORTED_FUNCTIONS="$EXPORTS"
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8","UTF8ToString","stringToUTF8"]'
  -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=4194304 -sSTACK_SIZE=262144
  -sFILESYSTEM=0 -sINCOMING_MODULE_JS_API='["locateFile","wasmBinary"]'
  -o sim/dist/irl2fa.mjs
)

if ! command -v emcc >/dev/null 2>&1; then
  echo "== emcc not on PATH: running this script in docker $EMSDK_IMAGE"
  exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e EM_CACHE=/tmp/emcache \
    -v "$ROOT:/src" -w /src "$EMSDK_IMAGE" \
    bash -c 'cp -r /emsdk/upstream/emscripten/cache /tmp/emcache 2>/dev/null || true; bash sim/build.sh'
fi

rm -rf "$DIST"
mkdir -p "$DIST"
echo "== $(emcc --version | head -n1)"
# shellcheck disable=SC2086
(cd "$ROOT" && emcc $CORE "${EMFLAGS[@]}")

# The static page (and sim/web/CNAME, if one has been chosen, for the
# GitHub Pages custom domain).
cp -r "$SIM"/web/. "$DIST"/
# The lead's power model, if it exists, overrides the defaults.
if [ -f "$ROOT/hardware/power/model.json" ]; then
  cp "$ROOT/hardware/power/model.json" "$DIST/power-model.json"
fi
touch "$DIST/.nojekyll"

echo "== built:"
ls -l "$DIST"
