# Host build: unit tests. The device build lives in port/stm32u0 (CMake).
# SPDX-License-Identifier: AGPL-3.0-or-later

CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer

CORE = core/hash.c core/base32.c core/totp.c core/words.c core/wordlist.c \
       core/pairing.c core/store.c core/gfx.c core/fonts.c core/timeutil.c \
       core/app.c core/third_party/monocypher/monocypher.c
TESTS = tests/test_main.c tests/test_crypto.c tests/test_device.c vhal/vhal.c

BUILD = build/host

.PHONY: test clean assets
test: $(BUILD)/tests
	./$(BUILD)/tests

$(BUILD)/tests: $(CORE) $(TESTS) $(wildcard core/*.h tests/*.h tests/*.def vhal/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(CORE) $(TESTS)

assets:
	python3 tools/gen_assets.py

clean:
	rm -rf build
