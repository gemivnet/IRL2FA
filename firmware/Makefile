# Host build: unit tests. The device build lives in port/stm32u0 (CMake).
# SPDX-License-Identifier: AGPL-3.0-or-later

CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer

CORE = core/hash.c core/base32.c core/totp.c core/words.c core/wordlist.c \
       core/pairing.c core/store.c core/gfx.c core/fonts.c core/timeutil.c \
       core/app.c core/buttons.c core/third_party/monocypher/monocypher.c
TESTS = tests/test_main.c tests/test_crypto.c tests/test_device.c tests/test_powercut.c \
        tests/test_app.c tests/test_behaviour.c vhal/vhal.c

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

# ---- verification pack (docs/hardware-testing.md) --------------------------
# Needs clang (libFuzzer), llvm-cov, valgrind, cppcheck and clang-tidy. The
# top-level `make verify` runs these targets in the firmware/verify image.
CLANG ?= clang
LLVM_COV ?= llvm-cov
LLVM_PROFDATA ?= llvm-profdata
VERIFY = build/verify
MONO = core/third_party/monocypher/monocypher.c
CORE_SRC = $(filter-out $(MONO),$(CORE))

.PHONY: verify fuzz fuzz-build ct coverage lint cppcheck clang-tidy

verify: lint ct coverage fuzz

# libFuzzer: each harness runs FUZZ_TIME seconds from its committed seeds
# (fuzz/corpus/<name>/, only read) into a scratch corpus. A crash, leak,
# sanitizer report or timeout fails, leaving the input in
# build/verify/crash-<name>-<sha1>.
FUZZERS = pairing store base32 app
FUZZ_TIME ?= 60
FUZZ_CFLAGS = -std=c11 -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
              -fno-omit-frame-pointer
FUZZ_SRC_pairing = core/pairing.c core/hash.c $(MONO)
FUZZ_SRC_base32 = core/base32.c
FUZZ_SRC_store = core/store.c core/hash.c core/buttons.c vhal/vhal.c $(MONO)
FUZZ_SRC_app = $(CORE) vhal/vhal.c
FUZZ_RUN = -max_total_time=$(FUZZ_TIME) -timeout=10 -rss_limit_mb=2048 -max_len=4096 \
           -print_final_stats=1

fuzz-build: $(FUZZERS:%=$(VERIFY)/fuzz_%)

$(VERIFY)/fuzz_%: fuzz/fuzz_%.c $(CORE) $(wildcard core/*.h vhal/*)
	@mkdir -p $(VERIFY)
	$(CLANG) $(FUZZ_CFLAGS) -o $@ $< $(FUZZ_SRC_$*)

fuzz: $(FUZZERS:%=fuzz-%)

fuzz-%: $(VERIFY)/fuzz_%
	@rm -rf $(VERIFY)/corpus-$*
	@mkdir -p $(VERIFY)/corpus-$*
	@echo "== fuzz_$* for $(FUZZ_TIME) s"
	@./$(VERIFY)/fuzz_$* $(FUZZ_RUN) -artifact_prefix=$(VERIFY)/crash-$*- \
	  $(VERIFY)/corpus-$* fuzz/corpus/$* > $(VERIFY)/fuzz-$*.log 2>&1 \
	  || { tail -n 60 $(VERIFY)/fuzz-$*.log; echo "fuzz_$* FAILED"; exit 1; }
	@grep -E '^(stat::|Done|INFO: (Loaded|seed corpus))' $(VERIFY)/fuzz-$*.log

# Constant-time check: valgrind memcheck with secrets marked undefined.
CT_CFLAGS = -std=c11 -O2 -g -DIRL2FA_CTGRIND -Wall -Wextra -Werror -Wno-unused-parameter
$(VERIFY)/ct_check: verify/ct_check.c verify/ct.supp $(CORE) $(wildcard core/*.h vhal/*)
	@mkdir -p $(VERIFY)
	$(CC) $(CT_CFLAGS) -fno-inline -c -o $(VERIFY)/monocypher-noinline.o $(MONO)
	$(CC) $(CT_CFLAGS) -o $@ verify/ct_check.c core/pairing.c core/store.c core/hash.c core/buttons.c \
	  vhal/vhal.c $(VERIFY)/monocypher-noinline.o

ct: $(VERIFY)/ct_check
	valgrind -q --suppressions=verify/ct.supp --track-origins=yes ./$(VERIFY)/ct_check

# Line and branch coverage of firmware/core under the host tests (clang
# source-based coverage). HTML report in build/verify/coverage/html; fails
# below COV_MIN_LINES / COV_MIN_BRANCHES percent.
COV_MIN_LINES ?= 92
COV_MIN_BRANCHES ?= 78
COV = $(VERIFY)/coverage
COV_ARGS = $(COV)/tests -instr-profile=$(COV)/tests.profdata \
           -ignore-filename-regex='third_party|fonts\.c|wordlist\.c|tests/|vhal/'
coverage:
	@mkdir -p $(COV)
	$(CLANG) -std=c11 -O0 -g -fprofile-instr-generate -fcoverage-mapping -o $(COV)/tests \
	  $(CORE) $(TESTS)
	LLVM_PROFILE_FILE=$(COV)/tests.profraw ./$(COV)/tests > /dev/null
	$(LLVM_PROFDATA) merge -o $(COV)/tests.profdata $(COV)/tests.profraw
	$(LLVM_COV) show $(COV_ARGS) -show-branches=count -format=html -output-dir=$(COV)/html
	$(LLVM_COV) report $(COV_ARGS) | tee $(COV)/summary.txt
	$(LLVM_COV) export $(COV_ARGS) -summary-only > $(COV)/summary.json
	python3 verify/coverage_gate.py $(COV)/summary.json $(COV_MIN_LINES) $(COV_MIN_BRANCHES)

# Static analysis, zero findings. Every suppression is inline, with a reason.
LINT_SRC = $(CORE_SRC) vhal/vhal.c
lint: cppcheck clang-tidy

cppcheck:
	cppcheck --std=c11 --enable=warning,style,performance,portability --inline-suppr \
	  --suppress=missingIncludeSystem --error-exitcode=1 -q -Icore -UMONOCYPHER_CPP_NAMESPACE \
	  $(LINT_SRC) verify/ct_check.c fuzz/*.c tests/*.c

clang-tidy:
	clang-tidy --quiet --warnings-as-errors='*' $(LINT_SRC) -- -std=c11 -Icore
