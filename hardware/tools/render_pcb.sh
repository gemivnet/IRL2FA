#!/usr/bin/env bash
# Renders docs/images/pcb/{pcb-front,pcb-back,pcb-iso}.png from the routed
# board with KiCad's raytracer. Needs Docker (the irl2fa-hw image).
# SPDX-License-Identifier: CERN-OHL-S-2.0
set -euo pipefail
cd "$(dirname "$0")/../.."
KICAD_IMAGE=${KICAD_IMAGE:-irl2fa-hw}
OUT=docs/images/pcb
mkdir -p "$OUT"
r() { docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -w /w "$KICAD_IMAGE" \
        kicad-cli pcb render --quality high --floor "$@" hardware/irl2fa.kicad_pcb >/dev/null; }
r --side top    -w 1544 -h 648 --zoom ${ZOOM:-2.0} -o "$OUT/pcb-front.png"
r --side bottom -w 1544 -h 648 --zoom ${ZOOM:-2.0} -o "$OUT/pcb-back.png"
r --side top    -w 1544 -h 952 --zoom 1.05 --perspective --rotate '-40,0,-25' -o "$OUT/pcb-iso.png"
echo "rendered $OUT/pcb-front.png pcb-back.png pcb-iso.png"
