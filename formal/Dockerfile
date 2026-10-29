# Tamarin prover + Maude for formal/run.sh. Every input is pinned: the base
# image by digest and both release downloads by SHA-256.
# SPDX-License-Identifier: AGPL-3.0-or-later
FROM ubuntu:24.04@sha256:24737cef17f4e22b1977df7d9a05e77c4e28b7da5b8911f8d8ccda136bf3573c

ARG TAMARIN_VERSION=1.12.0
ARG TAMARIN_SHA256=201be06f469e47cff554df6ca93db8366fc2c69d70c61fcbd1370a1074b469c6
ARG MAUDE_VERSION=3.5.1
ARG MAUDE_SHA256=72ed1ca87e3b3d0dfc6ee1436baf154bf04c45ff97d521bec040c5e8dfc8f92c

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl unzip libgmp10 libtinfo6 libffi8 zlib1g \
 && rm -rf /var/lib/apt/lists/*

RUN set -eux; cd /tmp; \
    curl -fsSLo tamarin.tar.gz "https://github.com/tamarin-prover/tamarin-prover/releases/download/${TAMARIN_VERSION}/tamarin-prover-${TAMARIN_VERSION}-linux64-ubuntu.tar.gz"; \
    echo "${TAMARIN_SHA256}  tamarin.tar.gz" | sha256sum -c -; \
    tar -xzf tamarin.tar.gz -C /usr/local/bin tamarin-prover; \
    curl -fsSLo maude.zip "https://github.com/maude-lang/Maude/releases/download/Maude${MAUDE_VERSION}/Maude-${MAUDE_VERSION}-linux-x86_64.zip"; \
    echo "${MAUDE_SHA256}  maude.zip" | sha256sum -c -; \
    mkdir -p /opt/maude; unzip -q maude.zip -d /opt/maude; chmod 0755 /opt/maude/maude; \
    ln -s /opt/maude/maude /usr/local/bin/maude; \
    rm -f tamarin.tar.gz maude.zip

ENV MAUDE_LIB=/opt/maude
CMD ["tamarin-prover", "--version"]
