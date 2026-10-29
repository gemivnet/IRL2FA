# Runs every check CI runs, locally. Needs Docker, a C compiler, Node 20+ and
# Python 3 (`python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt`).
# SPDX-License-Identifier: AGPL-3.0-or-later

PY ?= $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)
HW_IMAGE ?= irl2fa-hw
HW = docker run --rm -u $$(id -u):$$(id -g) -e HOME=/tmp -v "$(CURDIR)":/w -w /w $(HW_IMAGE)

.PHONY: check firmware stm32 renode sim case hardware hw-image fab pins screens power

check: firmware stm32 renode sim case hardware

firmware:
	$(MAKE) -C firmware test
	$(PY) hardware/tools/gen_pins.py --check

stm32:
	firmware/port/stm32u0/build.sh

renode: stm32
	SKIP_BUILD=1 firmware/renode/run.sh
	$(PY) hardware/power/power.py --measured firmware/build/renode --check

sim:
	sim/build.sh
	node --test 'sim/test/*.test.mjs'

case:
	case/build.sh --no-render
	$(PY) -m pytest -q case/tests

hw-image:
	docker build -q -t $(HW_IMAGE) hardware/ci

hardware: hw-image
	$(HW) kicad-cli sch erc --severity-error --exit-code-violations -o /tmp/erc.rpt hardware/irl2fa.kicad_sch
	$(HW) kicad-cli pcb drc --schematic-parity --severity-error --exit-code-violations -o /tmp/drc.rpt hardware/irl2fa.kicad_pcb
	$(HW) python3 -m pytest -q hardware/tests

# Regenerate committed outputs after a change.
fab: hw-image
	$(HW) python3 hardware/tools/fab.py

pins:
	$(PY) hardware/tools/gen_pins.py --docs

screens:
	$(PY) firmware/tools/screens.py

# After a firmware change that moves the measured power profile on purpose:
# re-measure in Renode and regenerate measured.json, the power doc, its chart
# and the simulator's power model.
power: stm32
	SKIP_BUILD=1 firmware/renode/run.sh
	$(PY) hardware/power/power.py --measured firmware/build/renode --write
