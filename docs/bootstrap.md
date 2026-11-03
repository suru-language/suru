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
module search, and four low-GC-threshold programs. Thus the repository's
current count is 130 unit checks plus 70 integration tests; the supplied
baseline claim of 120 unit checks is stale.

## Tool availability

GCC 12.2.0 is installed. Clang and `clang --analyze` were not installed in the
audit environment, so no Clang result is claimed. The Makefile's static
analysis target consequently cannot provide a result until Clang is available.

GCC's `-fanalyzer` was available and completed successfully with:

```text
gcc -std=c17 -Iinclude -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -fanalyzer -fsyntax-only src/*.c
```

The first sanitizer invocation using the existing `asan-test` target was
terminated by the environment after repeated AddressSanitizer fatal-signal
output. It is not recorded as a pass. Sanitizer verification must be repeated
with clean, isolated build directories before a release claim.

## Current bootstrap capabilities

- C17 lexer, recursive-descent parser, semantic analyser and bytecode compiler.
- Little-endian bytecode version 1 with a pre-execution verifier.
- Stack VM, managed tracing heap, standard-library builtins, and CLI commands
  `run`, `check`, `compile`, and `exec`.
- Legacy syntax only: `func`, `let`/`mut`, `if`/`else`, `for`, `while`, `type`,
  `use`, and related constructs.

Natural syntax, a SURU compiler source tree, a SURU-native VM, and a native
backend are not present at this revision. The phase-0 acceptance criteria are
therefore not complete; the remaining design and implementation gates are
tracked in `docs/syntax-migration.md` and `docs/self-hosting.md`.
