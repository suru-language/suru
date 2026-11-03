# Bootstrap Baseline

This document records the verified starting point for self-hosting work.

## Audit record

| item | result |
|---|---|
| repository | `suru-language/suru` checkout |
| revision | `b46790111f5e2c09679a034b704ea39cafff608f` |
| branch | `main` tracking `origin/main` |
| audit date | 2026-11-03 (Africa/Kampala) |
| C sources | 7,671 lines in `src/*.c` |
| documentation | 1,193 lines in `docs/*.md` |
| integration source files | 69 `.suru` files |
| unit test programs | 4 |
| unit checks | 130 |

The working tree already contained changes before this audit: deleted tracked
build outputs, a modified `tests/unit/test_bc.c`, and an untracked
`opencode.jsonc`. They are preserved. Generated files created by later checks
must not be used as baseline evidence.

## Commands and results

The following command was run from the repository root:

```text
make
```

It exited successfully; the existing debug binary was already up to date.

The full available default test suite was run with:

```text
make test
```

Result:

```text
test_bc        OK: 31 checks passed
test_gc        OK: 22 checks passed
test_lexparse  OK: 12 checks passed
test_sema      OK: 65 checks passed
integration: 70 tests, 0 failures
```

The integration runner's 70 tests include bytecode compile/exec, arguments,
module search, and four low-GC-threshold programs. The audit snapshot had 130
unit checks; the Phase 0 bounded-I/O test added since then brings the current
count to 139 unit checks plus 70 integration tests.

## Tool availability and verification

GCC 12.2.0, Clang 14.0.6, and CMake 3.25.1 are available in the verification
environment. Clang's static analyzer completed successfully with `make
analyze`. GCC's `-fanalyzer` also completed successfully with:

```text
gcc -std=c17 -Iinclude -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -fanalyzer -fsyntax-only src/*.c
```

Clean, isolated GCC and Clang AddressSanitizer/UndefinedBehaviorSanitizer
builds passed the full unit, integration, and self-hosted lexer suites. The
sanitizer link flags use non-PIE executables so AddressSanitizer shadow-memory
mapping is stable in constrained Linux environments. A GCC UBSan-only build
also passed the same suites. Clang debug and CMake debug/ sanitizer builds
passed their registered tests.

The completion commands were:

```text
make test
make analyze
make BUILD=build-gcc-release CFLAGS="... -O2 -DNDEBUG ..." test
make BUILD=build-gcc-ubsan CFLAGS="... -fsanitize=undefined ..." \
  LDFLAGS="-fsanitize=undefined" test
make BUILD=build-gcc-asan-nopie asan-test
make BUILD=build-clang CC=clang test
make BUILD=build-clang-asan-nopie CC=clang \
  CFLAGS="... -fsanitize=address,undefined ... -fno-pie ..." \
  LDFLAGS="-fsanitize=address,undefined -no-pie" test
cmake -S . -B build-cmake-clang -DCMAKE_C_COMPILER=clang
cmake --build build-cmake-clang
ctest --test-dir build-cmake-clang --output-on-failure
cmake -S . -B build-cmake-clang-asan -DCMAKE_C_COMPILER=clang \
  -DSU_SANITIZE=ON
cmake --build build-cmake-clang-asan
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-cmake-clang-asan \
  --output-on-failure
```

The abbreviated flag values above are the repository's `BASE`, `WARN`,
`RELEASE_CFLAGS`, `UBSAN_CFLAGS`, and `ASAN_CFLAGS` values from the Makefile.

## Current bootstrap capabilities

- C17 lexer, recursive-descent parser, semantic analyser and bytecode compiler.
- Little-endian bytecode version 1 with a pre-execution verifier.
- Stack VM, managed tracing heap, standard-library builtins, and CLI commands
  `run`, `check`, `compile`, and `exec`.
- Legacy syntax only: `func`, `let`/`mut`, `if`/`else`, `for`, `while`, `type`,
  `use`, and related constructs.

Natural syntax, a SURU compiler source tree, a SURU-native VM, and a native
backend are intentionally outside Phase 0. Phase 0 is complete: the C17
bootstrap baseline, specification audit, bounded binary I/O, reproducible
debug/release builds, static analysis, sanitizer checks, CMake portability
checks, and regression suites are recorded above. The remaining design and
implementation gates are tracked in `docs/syntax-migration.md` and
`docs/self-hosting.md`.
