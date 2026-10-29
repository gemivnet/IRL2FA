# Toolchain for the firmware verification pack (firmware/verify/README.md):
# clang + libFuzzer, llvm-cov, valgrind (constant-time checks), cppcheck and
# clang-tidy. Build: docker build -t irl2fa-verify firmware/verify
# SPDX-License-Identifier: AGPL-3.0-or-later
FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang-18 llvm-18 clang-tidy-18 libclang-rt-18-dev valgrind cppcheck make gcc libc6-dev \
      python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && ln -s /usr/bin/clang-18 /usr/local/bin/clang \
 && ln -s /usr/bin/clang-tidy-18 /usr/local/bin/clang-tidy \
 && ln -s /usr/bin/llvm-cov-18 /usr/local/bin/llvm-cov \
 && ln -s /usr/bin/llvm-profdata-18 /usr/local/bin/llvm-profdata
