#!/usr/bin/env bash
# Regenerates the schematic and PCB from tools/design.py, autoroutes with
# Freerouting and runs ERC/DRC. Needs Docker. This OVERWRITES
# hardware/irl2fa.kicad_sch and hardware/irl2fa.kicad_pcb: it is the bootstrap
# used to create them, not part of the normal edit-in-KiCad workflow.
# SPDX-License-Identifier: CERN-OHL-S-2.0
set -euo pipefail
cd "$(dirname "$0")/../.."
KICAD_IMAGE=${KICAD_IMAGE:-kicad/kicad:9.0}
JAVA_IMAGE=${JAVA_IMAGE:-eclipse-temurin:25-jre}
FR_VERSION=${FR_VERSION:-2.5.0}
PASSES=${PASSES:-60}
mkdir -p hardware/build
JAR=hardware/build/freerouting-$FR_VERSION.jar
if [ ! -f "$JAR" ]; then
  curl -sSfL -o "$JAR" "https://github.com/freerouting/freerouting/releases/download/v$FR_VERSION/freerouting-$FR_VERSION.jar"
fi
kicad() { docker run --rm -e SKIP_LOCAL -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -w /w "$KICAD_IMAGE" "$@"; }

if [ "${SKIP_SCHEMATIC:-0}" != 1 ]; then
  kicad python3 hardware/tools/gen_schematic.py
fi
kicad kicad-cli sch export netlist --format kicadsexpr -o hardware/build/irl2fa.net hardware/irl2fa.kicad_sch
kicad python3 hardware/tools/gen_pcb.py --step place
kicad python3 hardware/tools/gen_pcb.py --step dsn
docker run --rm -e SKIP_LOCAL -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -w /w "$JAVA_IMAGE" \
  java -jar "$JAR" -de hardware/build/irl2fa.dsn -do hardware/build/irl2fa.ses -mp "$PASSES" --gui.enabled=false
kicad python3 hardware/tools/gen_pcb.py --step ses
kicad python3 hardware/tools/gen_pcb.py --step finish
kicad kicad-cli sch erc --severity-error --exit-code-violations -o hardware/build/erc.rpt hardware/irl2fa.kicad_sch || { cat hardware/build/erc.rpt; exit 1; }
kicad kicad-cli pcb drc --schematic-parity --severity-error --exit-code-violations -o hardware/build/drc.rpt hardware/irl2fa.kicad_pcb || { cat hardware/build/drc.rpt; exit 1; }
echo "ERC and DRC clean"
