# SURU build system
#
# Usage:
#   make                debug build (default)
#   make release        optimized build
#   make asan          AddressSanitizer + UndefinedBehaviorSanitizer build
#   make ubsan         UndefinedBehaviorSanitizer build
#   make test          run unit + integration tests (current build flags)
#   make asan-test     ASan/UBSan build + full test suite
#   make analyze        static analysis (clang --analyze)
#   make clean

CC      ?= cc
BUILD   ?= build

 WARN     = -Wall -Wextra -Wpedantic -Wconversion -Wshadow
# -MMD -MP generates header dependency files for reliable incremental rebuilds.
# -fstack-protector-strong and -D_FORTIFY_SOURCE=2 are default build-time defenses.
BASE     = -std=c17 -Iinclude -MMD -MP -fstack-protector-strong -D_FORTIFY_SOURCE=2
CFLAGS  ?= $(BASE) -g -O0 $(WARN) -Werror
LDFLAGS ?=

RELEASE_CFLAGS = $(BASE) -O2 -DNDEBUG $(WARN)
ASAN_CFLAGS   = $(BASE) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie $(WARN) -Werror
ASAN_LDFLAGS  = -fsanitize=address,undefined -no-pie
UBSAN_CFLAGS  = $(BASE) -g -O1 -fsanitize=undefined -fno-omit-frame-pointer $(WARN) -Werror
UBSAN_LDFLAGS = -fsanitize=undefined

OBJDIR  := $(BUILD)/obj
BINDIR  := $(BUILD)/bin
UNITDIR := $(BUILD)/unit

LIBSRC = \
	src/arena.c \
	src/io.c \
	src/diag.c \
	src/lexer.c \
	src/parser.c \
	src/sema.c \
	src/bc.c \
	src/bcc.c \
	src/verify.c \
	src/mem.c \
	src/runtime.c \
	src/stdlib.c \
	src/vm.c \
	src/driver.c

LIBOBJ = $(LIBSRC:src/%.c=$(OBJDIR)/%.o)
UNIT_SRC = $(wildcard tests/unit/test_*.c)
SELFHOST_SRC = $(wildcard selfhost/compiler/*.suru)
SELFHOST_DRIVERS = tests/selfhost/lexer_driver.suru \
	tests/selfhost/token_stream_driver.suru \
	tests/selfhost/natural_lexer_driver.suru \
	tests/selfhost/natural_parser_driver.suru \
	tests/selfhost/natural_token_driver.suru \
	tests/selfhost/natural_expression_driver.suru \
	tests/selfhost/natural_ast_driver.suru \
	tests/selfhost/natural_control_driver.suru \
	tests/selfhost/natural_semantic_driver.suru \
	tests/selfhost/natural_declaration_driver.suru \
	tests/selfhost/natural_module_driver.suru \
	tests/selfhost/natural_effect_driver.suru \
	tests/selfhost/effect_runtime_driver.suru \
	tests/selfhost/natural_bytecode_driver.suru
SELFHOST_REF = $(UNITDIR)/selfhost_reference_lexer

all: $(BINDIR)/suru

$(OBJDIR) $(UNITDIR) $(BINDIR):
	mkdir -p $@

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/suru: $(LIBOBJ) $(OBJDIR)/main.o | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBOBJ) $(OBJDIR)/main.o -o $@

release:
	$(MAKE) CFLAGS="$(RELEASE_CFLAGS)"

asan:
	$(MAKE) CFLAGS="$(ASAN_CFLAGS)" LDFLAGS="$(ASAN_LDFLAGS)"

ubsan:
	$(MAKE) CFLAGS="$(UBSAN_CFLAGS)" LDFLAGS="$(UBSAN_LDFLAGS)"

.PHONY: unit
unit: $(LIBOBJ) | $(UNITDIR)
	@$(foreach t,$(UNIT_SRC), \
		echo "cc $(t)"; \
		$(CC) $(CFLAGS) -Itests/unit -c $(t) -o $(UNITDIR)/$(notdir $(basename $(t))).o && \
		$(CC) $(CFLAGS) $(LDFLAGS) -Itests/unit $(LIBOBJ) $(UNITDIR)/$(notdir $(basename $(t))).o -o $(UNITDIR)/$(notdir $(basename $(t))) && \
		$(UNITDIR)/$(notdir $(basename $(t))) || exit 1; )

.PHONY: integration
integration: $(BINDIR)/suru
	@tests/integration/run.sh $(BINDIR)/suru

# Run the integration suite with incremental GC enabled (SURU_GC_INCREMENTAL=1).
.PHONY: integration-incremental
integration-incremental: $(BINDIR)/suru
	@SURU_GC_INCREMENTAL=1 tests/integration/run.sh $(BINDIR)/suru

.PHONY: test
test: unit integration selfhost-test

.PHONY: selfhost-check
selfhost-check: $(BINDIR)/suru $(SELFHOST_SRC) $(SELFHOST_DRIVERS)
	@$(foreach d,$(SELFHOST_DRIVERS), \
		echo "check $(d)"; \
		$(BINDIR)/suru check "$(d)" >/dev/null || exit 1; )

$(SELFHOST_REF): tests/selfhost/reference_lexer.c $(LIBOBJ) | $(UNITDIR)
	$(CC) $(CFLAGS) -Itests/unit $< $(LIBOBJ) $(LDFLAGS) -o $@

.PHONY: selfhost-test
selfhost-test: selfhost-check $(SELFHOST_REF)
	@tests/selfhost/run.sh $(BINDIR)/suru $(SELFHOST_REF)
	@tests/selfhost/natural_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_parser_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_frontend_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_expression_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_ast_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_control_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_semantic_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_declaration_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_module_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_effect_run.sh $(BINDIR)/suru
	@tests/selfhost/effect_runtime_run.sh $(BINDIR)/suru
	@tests/selfhost/natural_bytecode_run.sh $(BINDIR)/suru

.PHONY: fuzz
fuzz: $(BINDIR)/suru | $(UNITDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -Itests/unit tests/fuzz/frontend_fuzz.c $(LIBOBJ) -o $(UNITDIR)/frontend_fuzz
	$(CC) $(CFLAGS) $(LDFLAGS) -Itests/unit tests/fuzz/verify_fuzz.c $(LIBOBJ) -o $(UNITDIR)/verify_fuzz
	$(UNITDIR)/verify_fuzz < /dev/urandom
	$(UNITDIR)/frontend_fuzz < /dev/urandom

# Coverage-guided fuzzing via libFuzzer (clang). `-fsanitize=fuzzer`
# provides the harness main and sets FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION,
# which selects the LLVMFuzzerTestOneInput entry point in the fuzz targets.
.PHONY: fuzz-libfuzzer
fuzz-libfuzzer: | $(BINDIR)
	@command -v clang >/dev/null 2>&1 || { echo "clang not found; skipping"; exit 0; }
	clang $(BASE) -g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer $(WARN) -Werror \
		-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION \
		$(LIBSRC) tests/fuzz/frontend_fuzz.c -o $(BINDIR)/frontend_ffz
	clang $(BASE) -g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer $(WARN) -Werror \
		-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION \
		$(LIBSRC) tests/fuzz/verify_fuzz.c -o $(BINDIR)/verify_ffz
	$(BINDIR)/frontend_ffz -max_total_time=20 -rss_limit_mb=1024 \
		tests/fuzz/corpus -print_final_stats=1
	$(BINDIR)/verify_ffz -max_total_time=20 -rss_limit_mb=1024 \
		tests/fuzz/corpus -print_final_stats=1

.PHONY: asan-test
asan-test:
	$(MAKE) CFLAGS="$(ASAN_CFLAGS)" LDFLAGS="$(ASAN_LDFLAGS)" \
		ASAN_OPTIONS=detect_leaks=1 test

.PHONY: analyze
analyze:
	@command -v clang >/dev/null 2>&1 || { echo "clang not found; skipping analyze"; exit 0; }
	@for s in $(LIBSRC); do \
		echo "analyze $$s"; \
		clang --analyze $(BASE) $$s || exit 1; \
	done

.PHONY: ci
ci:
	$(MAKE) clean && $(MAKE) test
	$(MAKE) clean && $(MAKE) release
	$(MAKE) clean && $(MAKE) asan-test
	$(MAKE) analyze
	$(MAKE) clean && $(MAKE) fuzz
	$(MAKE) clean && $(MAKE)

.PHONY: clean
clean:
	rm -rf $(BUILD)

# Header dependency files, included last so they do not change the default goal.
-include $(wildcard $(OBJDIR)/*.d)
