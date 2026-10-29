# KiCad 9 + the Python test tools used by hardware CI (ngspice is already in
# the KiCad image). Build: docker build -t irl2fa-hw hardware/ci
# SPDX-License-Identifier: CERN-OHL-S-2.0
FROM kicad/kicad:9.0
USER root
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3-pip python3-pytest python3-yaml python3-numpy git \
 && pip3 install --no-cache-dir --break-system-packages gerbonara==1.5.0 InteractiveHtmlBom==2.10.0 \
 && rm -rf /var/lib/apt/lists/*

# Make the stock KiCad libraries resolvable by kicad-cli (DRC footprint checks).
RUN mkdir -p /home/kicad/.config/kicad/9.0 \
 && cp /usr/share/kicad/template/fp-lib-table /usr/share/kicad/template/sym-lib-table /home/kicad/.config/kicad/9.0/ \
 && chmod -R a+rwX /home/kicad/.config
ENV KICAD_CONFIG_HOME=/home/kicad/.config/kicad
USER kicad
