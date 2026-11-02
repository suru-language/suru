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
BASE     = -std=c17 -Iinclude -MMD -MP
CFLAGS  ?= $(BASE) -g -O0 $(WARN) -Werror
LDFLAGS ?=

RELEASE_CFLAGS = $(BASE) -O2 -DNDEBUG $(WARN)
ASAN_CFLAGS   = $(BASE) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) -Werror
ASAN_LDFLAGS  = -fsanitize=address,undefined
UBSAN_CFLAGS  = $(BASE) -g -O1 -fsanitize=undefined -fno-omit-frame-pointer $(WARN) -Werror
UBSAN_LDFLAGS = -fsanitize=undefined

OBJDIR  := $(BUILD)/obj
BINDIR  := $(BUILD)/bin
UNITDIR := $(BUILD)/unit

LIBSRC = \
	src/arena.c \
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

.PHONY: test
test: unit integration

.PHONY: fuzz
fuzz: $(BINDIR)/suru | $(UNITDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -Itests/unit tests/fuzz/frontend_fuzz.c $(LIBOBJ) -o $(UNITDIR)/frontend_fuzz
	$(CC) $(CFLAGS) $(LDFLAGS) -Itests/unit tests/fuzz/verify_fuzz.c $(LIBOBJ) -o $(UNITDIR)/verify_fuzz
	$(UNITDIR)/verify_fuzz < /dev/urandom
	$(UNITDIR)/frontend_fuzz < /dev/urandom

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
