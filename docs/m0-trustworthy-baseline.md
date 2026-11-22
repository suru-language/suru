# M0 trustworthy baseline — specification and coverage

## Stage 0 checkpoint

`docs/bootstrap.md` records the Stage 0 C17 bootstrap audit at revision `b46790111f5e2c09679a034b704ea39cafff608f`, dated 2026-11-03. It describes a bootstrap compiler, bytecode verifier, v1 VM, tracing heap and bounded binary I/O. Its historical test totals and sanitizer/analyzer outcomes belong to that snapshot, not to M0.

M0 is based on published `main` at `b0fec6d359694d088dae677be92272f762c917b5`. The requested plan file was not tracked on public refs, and `.kilo/` is gitignored. The resolved plan is reconstructed at the exact requested path from this task's seven workstreams.

## Bytecode v1 contract

Authority: `include/suru/bc.h`, `src/bc.c`, `docs/bytecode.md`.

- Four-byte magic `SURU`.
- Little-endian `u32` version exactly `S_BC_VERSION == 1`.
- Header: entry function index, then integer/float/string/record/function counts.
- Integers: signed 64-bit little-endian; floats: IEEE binary64 bit patterns little-endian.
- Strings: `u32` byte length and raw bytes.
- Records: field count only; record names are not serialized.
- Functions: name length/bytes, `nparams`, `frame_size`, `max_operand_depth`, `nins`.
- Instructions: opcode byte and three little-endian `u32` words (`a`, `b`, `c`).
- The verifier checks opcode/operand validity, references, stack discipline and branch-merge depths before VM execution.

Slot types and labels are compiler-only and are not serialized in bytecode v1.

## All 73 opcode IDs

Ordinals are enum ordinals. Stack effects are base metadata from `s_bc_op_table`; calls, builtins, arrays and closure instructions also have operand-dependent stack counts.

| Ordinal | Enum | Mnemonic | Operand words | Base stack effect | Flow |
|---:|---|---|---:|---:|---|
| 0 | `BC_NOP` | `nop` | 0 | 0 → 0 | fall-through |
| 1 | `BC_CONST_INT` | `const_int` | 1 | 0 → 1 | fall-through |
| 2 | `BC_CONST_FLOAT` | `const_float` | 1 | 0 → 1 | fall-through |
| 3 | `BC_CONST_STR` | `const_str` | 1 | 0 → 1 | fall-through |
| 4 | `BC_CONST_NONE` | `const_none` | 0 | 0 → 1 | fall-through |
| 5 | `BC_LD_LOCAL` | `ld_local` | 1 | 0 → 1 | fall-through |
| 6 | `BC_ST_LOCAL` | `st_local` | 1 | 1 → 0 | fall-through |
| 7 | `BC_POP` | `pop` | 0 | 1 → 0 | fall-through |
| 8 | `BC_DUP` | `dup` | 0 | 0 → 1 | fall-through |
| 9 | `BC_SWAP` | `swap` | 0 | 0 → 0 | fall-through |
| 10 | `BC_NOT` | `not` | 0 | 1 → 1 | fall-through |
| 11 | `BC_NEG_I` | `neg_i` | 0 | 1 → 1 | fall-through |
| 12 | `BC_NEG_F` | `neg_f` | 0 | 1 → 1 | fall-through |
| 13 | `BC_CONV_I_F` | `conv_i_f` | 0 | 1 → 1 | fall-through |
| 14 | `BC_ADD_I` | `add_i` | 0 | 2 → 1 | fall-through |
| 15 | `BC_SUB_I` | `sub_i` | 0 | 2 → 1 | fall-through |
| 16 | `BC_MUL_I` | `mul_i` | 0 | 2 → 1 | fall-through |
| 17 | `BC_DIV_I` | `div_i` | 0 | 2 → 1 | fall-through |
| 18 | `BC_MOD_I` | `mod_i` | 0 | 2 → 1 | fall-through |
| 19 | `BC_ADD_F` | `add_f` | 0 | 2 → 1 | fall-through |
| 20 | `BC_SUB_F` | `sub_f` | 0 | 2 → 1 | fall-through |
| 21 | `BC_MUL_F` | `mul_f` | 0 | 2 → 1 | fall-through |
| 22 | `BC_DIV_F` | `div_f` | 0 | 2 → 1 | fall-through |
| 23 | `BC_BIT_AND` | `bit_and` | 0 | 2 → 1 | fall-through |
| 24 | `BC_BIT_OR` | `bit_or` | 0 | 2 → 1 | fall-through |
| 25 | `BC_BIT_XOR` | `bit_xor` | 0 | 2 → 1 | fall-through |
| 26 | `BC_SHL` | `shl` | 0 | 2 → 1 | fall-through |
| 27 | `BC_SHR` | `shr` | 0 | 2 → 1 | fall-through |
| 28 | `BC_BIT_NOT` | `bit_not` | 0 | 1 → 1 | fall-through |
| 29 | `BC_EQ_I` | `eq_i` | 0 | 2 → 1 | fall-through |
| 30 | `BC_NE_I` | `ne_i` | 0 | 2 → 1 | fall-through |
| 31 | `BC_LT_I` | `lt_i` | 0 | 2 → 1 | fall-through |
| 32 | `BC_LE_I` | `le_i` | 0 | 2 → 1 | fall-through |
| 33 | `BC_GT_I` | `gt_i` | 0 | 2 → 1 | fall-through |
| 34 | `BC_GE_I` | `ge_i` | 0 | 2 → 1 | fall-through |
| 35 | `BC_EQ_F` | `eq_f` | 0 | 2 → 1 | fall-through |
| 36 | `BC_NE_F` | `ne_f` | 0 | 2 → 1 | fall-through |
| 37 | `BC_LT_F` | `lt_f` | 0 | 2 → 1 | fall-through |
| 38 | `BC_LE_F` | `le_f` | 0 | 2 → 1 | fall-through |
| 39 | `BC_GT_F` | `gt_f` | 0 | 2 → 1 | fall-through |
| 40 | `BC_GE_F` | `ge_f` | 0 | 2 → 1 | fall-through |
| 41 | `BC_EQ_S` | `eq_s` | 0 | 2 → 1 | fall-through |
| 42 | `BC_NE_S` | `ne_s` | 0 | 2 → 1 | fall-through |
| 43 | `BC_LT_S` | `lt_s` | 0 | 2 → 1 | fall-through |
| 44 | `BC_LE_S` | `le_s` | 0 | 2 → 1 | fall-through |
| 45 | `BC_GT_S` | `gt_s` | 0 | 2 → 1 | fall-through |
| 46 | `BC_GE_S` | `ge_s` | 0 | 2 → 1 | fall-through |
| 47 | `BC_CONCAT_S` | `concat_s` | 0 | 2 → 1 | fall-through |
| 48 | `BC_REPEAT_S` | `repeat_s` | 0 | 2 → 1 | fall-through |
| 49 | `BC_EQ_B` | `eq_b` | 0 | 2 → 1 | fall-through |
| 50 | `BC_NE_B` | `ne_b` | 0 | 2 → 1 | fall-through |
| 51 | `BC_NEW_ARRAY` | `new_array` | 1 | 1 → 1 | fall-through |
| 52 | `BC_NEW_RECORD` | `new_record` | 2 | 1 → 1 | fall-through |
| 53 | `BC_NEW_EXCEPTION` | `new_exception` | 0 | 3 → 1 | fall-through |
| 54 | `BC_IDX_LD` | `idx_ld` | 0 | 2 → 1 | fall-through |
| 55 | `BC_IDX_ST` | `idx_st` | 0 | 3 → 0 | fall-through |
| 56 | `BC_ARR_LEN` | `arr_len` | 0 | 1 → 1 | fall-through |
| 57 | `BC_FIELD_LD` | `field_ld` | 2 | 1 → 1 | fall-through |
| 58 | `BC_FIELD_ST` | `field_st` | 2 | 2 → 0 | fall-through |
| 59 | `BC_CALL` | `call` | 1 | 1 → 1 | fall-through |
| 60 | `BC_MKCLOSURE` | `mkclosure` | 1 | 0 → 1 | fall-through |
| 61 | `BC_MKCLOSURE_V` | `mkclosure_v` | 2 | 1 → 1 | fall-through |
| 62 | `BC_CALLV` | `call_v` | 1 | 1 → 1 | fall-through |
| 63 | `BC_BUILTIN` | `builtin` | 1 | 1 → 1 | fall-through |
| 64 | `BC_RET` | `ret` | 0 | 1 → 0 | terminal |
| 65 | `BC_RET_UNIT` | `ret_unit` | 0 | 0 → 0 | terminal |
| 66 | `BC_JMP` | `jmp` | 1 | 0 → 0 | branch |
| 67 | `BC_JZ` | `jz` | 1 | 1 → 0 | branch |
| 68 | `BC_JNZ` | `jnz` | 1 | 1 → 0 | branch |
| 69 | `BC_PUSH_HANDLER` | `push_handler` | 2 | 0 → 0 | fall-through |
| 70 | `BC_POP_HANDLER` | `pop_handler` | 0 | 0 → 0 | fall-through |
| 71 | `BC_THROW` | `throw` | 0 | 1 → 0 | terminal |
| 72 | `BC_TRAP` | `trap` | 0 | 0 → 0 | terminal |

Total: **73 opcodes**, cross-checked between enum and operation metadata.

## All 57 builtin IDs

IDs are stable ordinals in `include/suru/stdlib.h`; names and signatures come from `src/stdlib.c`; behavior from `docs/builtins.md` and VM dispatch. The final three had ID/signature entries but no behavior rows; M0 documents them.

| ID | Enum | Name | Signature | Actual behavior |
|---:|---|---|---|---|
| 0 | `S_BUILTIN_PRINT` | `print` | `(any) -> unit` | Formats a scalar/object and writes without a newline. |
| 1 | `S_BUILTIN_PRINTLN` | `println` | `(any) -> unit` | `print` followed by a newline. |
| 2 | `S_BUILTIN_STR_LEN` | `str_len` | `(str) -> int` | Returns the string byte length. |
| 3 | `S_BUILTIN_ARRAY_LEN` | `array_len` | `(array) -> int` | Returns the array element count. |
| 4 | `S_BUILTIN_READ_LINE` | `read_line` | `() -> str` | Reads stdin through the runtime line reader. EOF is an empty line. |
| 5 | `S_BUILTIN_TO_STR` | `to_str` | `(any) -> str` | Current implementation accepts int, float, or bool. Other values trap. |
| 6 | `S_BUILTIN_F64_TO_STR` | `f64_to_str` | `(float) -> str` | Formats a float with the runtime's shortest available representation. |
| 7 | `S_BUILTIN_I64_TO_F64` | `i64_to_f64` | `(int) -> float` | Numeric conversion. |
| 8 | `S_BUILTIN_F64_TO_I64` | `f64_to_i64` | `(float) -> int` | C-style numeric conversion to signed integer. |
| 9 | `S_BUILTIN_MAKE_ARRAY` | `make_array` | `(int) -> [int]` | Creates a zero-initialized array; length must be 0..1,048,576. |
| 10 | `S_BUILTIN_RANGE` | `range` | `(int, int) -> [int]` | Creates `[lo, hi)`; descending ranges are empty. |
| 11 | `S_BUILTIN_SUBSTR` | `substr` | `(str, int, int) -> str` | Byte slice; negative start/length clamp to zero and overrun clamps to end. |
| 12 | `S_BUILTIN_STR_CONTAINS` | `str_contains` | `(str, str) -> bool` | Substring search; the empty needle is present. |
| 13 | `S_BUILTIN_INT_FROM_STR` | `int_from_str` | `(str) -> int` | Strict decimal parse with optional sign; malformed/trailing input returns 0, overflow traps. |
| 14 | `S_BUILTIN_ABS` | `abs` | `(int) -> int` | Absolute value; `INT64_MIN` traps. |
| 15 | `S_BUILTIN_MIN` | `min` | `(int, int) -> int` | Returns the smaller integer. |
| 16 | `S_BUILTIN_MAX` | `max` | `(int, int) -> int` | Returns the larger integer. |
| 17 | `S_BUILTIN_STR_TO_F64` | `str_to_f64` | `(str) -> float` | Parses using the host floating parser; the current implementation accepts a bounded prefix. |
| 18 | `S_BUILTIN_RANGE_STEP` | `range_step` | `(int, int, int) -> [int]` | Stepped half-open range; zero step and excessive output trap. |
| 19 | `S_BUILTIN_ASSERT` | `assert` | `(bool, str) -> unit` | Does nothing when true; false traps with the supplied message. |
| 20 | `S_BUILTIN_EXIT` | `exit` | `(int) -> unit` | Stops the VM with the requested process exit code. |
| 21 | `S_BUILTIN_TO_HEX` | `to_hex` | `(int) -> str` | Lowercase hexadecimal representation without a prefix. |
| 22 | `S_BUILTIN_TO_BIN` | `to_bin` | `(int) -> str` | Binary representation without a prefix. |
| 23 | `S_BUILTIN_F64_TO_STR_PREC` | `f64_to_str_prec` | `(float, int) -> str` | Fixed-point formatting; precision clamps to 0..20. |
| 24 | `S_BUILTIN_STR_REPEAT` | `str_repeat` | `(str, int) -> str` | Repeats a string; count must be nonnegative and output is bounded. |
| 25 | `S_BUILTIN_STR_TRIM` | `str_trim` | `(str) -> str` | Removes ASCII space, tab, CR, and LF at both ends. |
| 26 | `S_BUILTIN_ARGS_LEN` | `args_len` | `() -> int` | Returns the arguments supplied to the `run` command. |
| 27 | `S_BUILTIN_ARGS` | `args` | `(int) -> str` | Returns an argument by index; out of range traps. |
| 28 | `S_BUILTIN_STR_SPLIT` | `str_split` | `(str, str) -> [str]` | Splits on a non-empty byte separator, retaining empty fields. |
| 29 | `S_BUILTIN_STR_JOIN` | `str_join` | `([str], str) -> str` | Joins a string array; non-string elements and excessive output trap. |
| 30 | `S_BUILTIN_ARRAY_SLICE` | `slice` | `([T], int, int) -> [T]` | Copies a clamped array slice. |
| 31 | `S_BUILTIN_STR_REVERSE` | `str_reverse` | `(str) -> str` | Reverses bytes, not Unicode scalar values. |
| 32 | `S_BUILTIN_STR_UPPER` | `str_upper` | `(str) -> str` | Converts ASCII lower-case bytes to upper-case. |
| 33 | `S_BUILTIN_STR_LOWER` | `str_lower` | `(str) -> str` | Converts ASCII upper-case bytes to lower-case. |
| 34 | `S_BUILTIN_LEN` | `len` | `(any) -> int` | Returns length for strings and arrays; other values trap. |
| 35 | `S_BUILTIN_STR_COMPARE` | `str_compare` | `(str, str) -> int` | Lexicographic byte comparison, returning negative, zero, or positive. |
| 36 | `S_BUILTIN_TYPE_OF` | `type_of` | `(any) -> str` | Returns `int`, `float`, `bool`, `unit`, `str`, `array`, `record`, or `closure`. |
| 37 | `S_BUILTIN_ARRAY_REVERSE` | `array_reverse` | `([T]) -> [T]` | Returns a reversed copy. |
| 38 | `S_BUILTIN_ARRAY_SUM` | `array_sum` | `([int]) -> int` | Sums a non-empty integer array. |
| 39 | `S_BUILTIN_ARRAY_MIN` | `array_min` | `([int]) -> int` | Minimum of a non-empty integer array. |
| 40 | `S_BUILTIN_ARRAY_MAX` | `array_max` | `([int]) -> int` | Maximum of a non-empty integer array. |
| 41 | `S_BUILTIN_READ_FILE` | `read_file` | `(str) -> str` | Reads a binary file into a string, capped at 64 MiB; open/seek/read errors trap. |
| 42 | `S_BUILTIN_WRITE_FILE` | `write_file` | `(str, str) -> unit` | Writes all string bytes to a path; open/write/close errors trap. |
| 43 | `S_BUILTIN_FILE_EXISTS` | `file_exists` | `(str) -> bool` | Tests whether a path can be opened for reading. |
| 44 | `S_BUILTIN_INT_TO_STR_BASE` | `int_to_str_base` | `(int, int) -> str` | Formats an integer in base 2..36 using lower-case digits. |
| 45 | `S_BUILTIN_STR_PAD` | `str_pad` | `(str, int, str) -> str` | Left-pads to a byte width using the first fill byte; excessive width traps. |
| 46 | `S_BUILTIN_ARRAY_CONCAT` | `array_concat` | `([T], [T]) -> [T]` | Returns a copied concatenation. |
| 47 | `S_BUILTIN_ARRAY_CONTAINS` | `array_contains` | `([int], int) -> bool` | Returns 1/0 for integer membership. |
| 48 | `S_BUILTIN_ARRAY_INDEX_OF` | `array_index_of` | `([int], int) -> int` | Returns the first index or -1. |
| 49 | `S_BUILTIN_ARRAY_SORT` | `array_sort` | `([int]) -> [int]` | Returns a sorted copy of an integer array. |
| 50 | `S_BUILTIN_STR_VALID_UTF8` | `str_valid_utf8` | `(str) -> bool` | Validates UTF-8 scalar encoding, rejecting overlong forms, surrogates, and out-of-range code points. |
| 51 | `S_BUILTIN_STR_UTF8_LEN` | `str_utf8_len` | `(str) -> int` | Returns the Unicode scalar count; invalid UTF-8 traps. |
| 52 | `S_BUILTIN_STR_FROM_CODEPOINT` | `str_from_codepoint` | `(int) -> str` | Encodes a Unicode scalar value as UTF-8; negatives, values above U+10FFFF, and surrogates trap. |
| 53 | `S_BUILTIN_STR_CHR_BYTE` | `str_chr_byte` | `(int) -> str` | Returns the single raw byte 0..255 (not UTF-8 encoded); out-of-range traps. Enables binary serialization. |
| 54 | `S_BUILTIN_F64_TO_BYTES` | `f64_to_bytes` | `(float) -> str` | Returns the eight raw little-endian bytes of the binary64 bit pattern. |
| 55 | `S_BUILTIN_STR_BYTE_AT` | `str_byte_at` | `(str, int) -> int` | Returns the unsigned byte at a zero-based index; negative and out-of-range indices trap. |
| 56 | `S_BUILTIN_BYTES_TO_F64` | `bytes_to_f64` | `(str) -> float` | Decodes the first eight bytes as a little-endian binary64 value; strings shorter than eight bytes trap. |

Total: **57 builtins**.

## Execution paths

### Legacy source/bytecode CLI

- `suru run file.suru [args...]`: bounded read in `src/main.c`, `s_driver_compile` (parse → sema → bytecode generation → verifier), then `s_driver_run_module_args` (heap/VM).
- `suru check file.suru`: compile and verify, without VM execution.
- `suru compile file.suru file.sbc`: compile, verify, serialize using `s_bc_write`.
- `suru exec file.sbc [args...]`: bounded bytecode read, `s_bc_read`, validation, driver/VM.
- `use "module.suru"`: absolute path, containing-file-relative path, then colon-separated `SURU_MOD_PATH`; included files are de-duplicated and share the compiler arena.

### Natural and self-host

`--syntax=natural` is a separate frontend under `tests/selfhost/` and `selfhost/compiler/`. It has Natural lex/parse/diagnostic tests, sema, bytecode writer/verifier, self-host rebuild, VM and native ELF execution paths. These remain in their dedicated runner scripts.

### Runtime/GC

`src/vm.c` interprets verified bytecode. Managed strings, arrays and records live in the heap; VM stack and explicit roots are registered for tracing. Collection can be triggered by allocation cadence or live-byte threshold and can run incrementally. Default cadence is 512 managed allocation attempts. M0 adds `SURU_GC_ALLOC_INTERVAL=1` to test managed-object reachability at each allocation; `SURU_GC_THRESHOLD` retains low-threshold stress and `SURU_GC_INCREMENTAL=1` selects incremental collection.

## Actual test coverage inventory

Counts are tracked source/assets, not claims of test success.

| Area | Count |
|---|---:|
| C implementation translation units | 15 |
| Existing C unit test programs | 6 |
| C unit programs after ownership test | 7 |
| Static `CHECK*` macro call sites after additions | 157 |
| Top-level legacy integration sources after M0 | 71 |
| Exact stdout goldens after M0 | 60 |
| Legacy sources without stdout goldens, explicitly unsupported | 11 |
| Manifest executions (one allocation-interval-one repeat) | 72 |
| Natural source fixtures | 64 |
| Natural diagnostic goldens | 11 |
| Natural stdout goldens | 2 |
| Self-host shell test drivers | 30 |
| Fuzz harnesses | 2 (`frontend_fuzz.c`, `verify_fuzz.c`) |
| Starting fuzz corpus seed files | 6,650 |

The six pre-existing unit suites cover bytecode, effect VM, GC, I/O, lexer/parser and sema. The CHECK count is a source inventory, not a test-run metric. The integration shell runner retains generated syntax/CLI, bytecode round-trip and Natural-path tests beyond the JSON file corpus. Self-host and fuzz suites remain separate gates.

`docs/bootstrap.md` records historical Stage 0 figures of 130 unit checks + 70 integration tests, then 139 unit checks + 70 integration tests after earlier I/O changes. These remain historical only.

## M0 invariants

1. Lexer identifiers and decoded string literals are compilation-arena-owned; token text no longer borrows source-buffer slices.
2. Include lookup probes metadata only; after de-duplication each source is read once. The bounded read allocation is reused when adding its NUL terminator.
3. AST names/string values remain valid after included source buffers are freed. The interval-one GC case keeps the `read_file` result reachable across repeated managed allocations; the current CLI prints boolean true as `1`, so its exact golden is `1\n1\n`.
4. Each function records parameter/source-local slot types and exact semantic frame count. Codegen uses that count instead of a second AST slot walk.
5. Branch patches are bounds-checked; unresolved labels fail before verifier entry.
6. Compiler metadata is not serialized; bytecode v1 is preserved.
7. I/O performs one exact-size `fread` and rejects short reads; write retry behavior is unchanged.
8. JSON corpus stdout comparisons are exact when a golden exists; missing goldens have an explicit unsupported reason.

## Required M0 verification gates

Run in a scratch clone and retain exact command/output:

1. `make clean && make test`
2. `SURU_GC_INCREMENTAL=1 make integration`
3. Manifest's `SURU_GC_ALLOC_INTERVAL=1` liveness test
4. `make BUILD=build-asan asan-test`
5. `make analyze`
6. `make determinism-test`
7. `make CC=clang BUILD=build-clang test`
8. Clean worktree: `git diff --exit-code` and empty `git status --porcelain`.

Unexecuted gates remain blocked, not passed.
