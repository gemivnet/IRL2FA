# KiCad 9 + the Python test tools used by hardware CI (ngspice is already in
# the KiCad image). Build: docker build -t irl2fa-hw hardware/ci
# SPDX-License-Identifier: CERN-OHL-S-2.0
FROM kicad/kicad:9.0
USER root
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3-pip python3-pytest python3-yaml python3-numpy git \
 && pip3 install --no-cache-dir --break-system-packages gerbonara==1.5.0 InteractiveHtmlBom==2.10.0 \
 && rm -rf /var/lib/apt/lists/*
USER kicad
