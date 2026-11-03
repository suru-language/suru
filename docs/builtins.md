# Bootstrap Builtins

The table in `src/stdlib.c` is the signature authority and `dispatch_builtin`
in `src/vm.c` is the runtime authority. Builtins are resolved by name during
semantic analysis and encoded by numeric ID in bytecode.

All failures below are controlled VM traps. `int` is signed 64-bit, `float` is
binary64, strings are byte sequences carried by the runtime string object, and
array lengths are bounded by the heap/runtime limits.

| builtin | signature | behavior and failure cases |
|---|---|---|
| `print` | `(any) -> unit` | Formats a scalar/object and writes without a newline. |
| `println` | `(any) -> unit` | `print` followed by a newline. |
| `str_len` | `(str) -> int` | Returns the string byte length. |
| `array_len` | `(array) -> int` | Returns the array element count. |
| `read_line` | `() -> str` | Reads stdin through the runtime line reader. EOF is an empty line. |
| `to_str` | `(any) -> str` | Current implementation accepts int, float, or bool. Other values trap. |
| `f64_to_str` | `(float) -> str` | Formats a float with the runtime's shortest available representation. |
| `i64_to_f64` | `(int) -> float` | Numeric conversion. |
| `f64_to_i64` | `(float) -> int` | C-style numeric conversion to signed integer. |
| `make_array` | `(int) -> [int]` | Creates a zero-initialized array; length must be 0..1,048,576. |
| `range` | `(int, int) -> [int]` | Creates `[lo, hi)`; descending ranges are empty. |
| `substr` | `(str, int, int) -> str` | Byte slice; negative start/length clamp to zero and overrun clamps to end. |
| `str_contains` | `(str, str) -> bool` | Substring search; the empty needle is present. |
| `int_from_str` | `(str) -> int` | Strict decimal parse with optional sign; malformed/trailing input returns 0, overflow traps. |
| `abs` | `(int) -> int` | Absolute value; `INT64_MIN` traps. |
| `min` | `(int, int) -> int` | Returns the smaller integer. |
| `max` | `(int, int) -> int` | Returns the larger integer. |
| `str_to_f64` | `(str) -> float` | Parses using the host floating parser; the current implementation accepts a bounded prefix. |
| `range_step` | `(int, int, int) -> [int]` | Stepped half-open range; zero step and excessive output trap. |
| `assert` | `(bool, str) -> unit` | Does nothing when true; false traps with the supplied message. |
| `exit` | `(int) -> unit` | Stops the VM with the requested process exit code. |
| `to_hex` | `(int) -> str` | Lowercase hexadecimal representation without a prefix. |
| `to_bin` | `(int) -> str` | Binary representation without a prefix. |
| `f64_to_str_prec` | `(float, int) -> str` | Fixed-point formatting; precision clamps to 0..20. |
| `str_repeat` | `(str, int) -> str` | Repeats a string; count must be nonnegative and output is bounded. |
| `str_trim` | `(str) -> str` | Removes ASCII space, tab, CR, and LF at both ends. |
| `args_len` | `() -> int` | Returns the arguments supplied to the `run` command. |
| `args` | `(int) -> str` | Returns an argument by index; out of range traps. |
| `str_split` | `(str, str) -> [str]` | Splits on a non-empty byte separator, retaining empty fields. |
| `str_join` | `([str], str) -> str` | Joins a string array; non-string elements and excessive output trap. |
| `slice` | `([T], int, int) -> [T]` | Copies a clamped array slice. |
| `str_reverse` | `(str) -> str` | Reverses bytes, not Unicode scalar values. |
| `str_upper` | `(str) -> str` | Converts ASCII lower-case bytes to upper-case. |
| `str_lower` | `(str) -> str` | Converts ASCII upper-case bytes to lower-case. |
| `len` | `(any) -> int` | Returns length for strings and arrays; other values trap. |
| `str_compare` | `(str, str) -> int` | Lexicographic byte comparison, returning negative, zero, or positive. |
| `type_of` | `(any) -> str` | Returns `int`, `float`, `bool`, `unit`, `str`, `array`, `record`, or `closure`. |
| `array_reverse` | `([T]) -> [T]` | Returns a reversed copy. |
| `array_sum` | `([int]) -> int` | Sums a non-empty integer array. |
| `array_min` | `([int]) -> int` | Minimum of a non-empty integer array. |
| `array_max` | `([int]) -> int` | Maximum of a non-empty integer array. |
| `read_file` | `(str) -> str` | Reads a binary file into a string, capped at 64 MiB; open/seek/read errors trap. |
| `write_file` | `(str, str) -> unit` | Writes all string bytes to a path; open/write/close errors trap. |
| `file_exists` | `(str) -> bool` | Tests whether a path can be opened for reading. |
| `int_to_str_base` | `(int, int) -> str` | Formats an integer in base 2..36 using lower-case digits. |
| `str_pad` | `(str, int, str) -> str` | Left-pads to a byte width using the first fill byte; excessive width traps. |
| `array_concat` | `([T], [T]) -> [T]` | Returns a copied concatenation. |
| `array_contains` | `([int], int) -> bool` | Returns 1/0 for integer membership. |
| `array_index_of` | `([int], int) -> int` | Returns the first index or -1. |
| `array_sort` | `([int]) -> [int]` | Returns a sorted copy of an integer array. |

The table intentionally records current behavior, including byte-oriented
string transforms and the bounded host-file builtins. The future Natural
Syntax/Document A standard-library contract must either adopt or explicitly
change each item with matching conformance tests.
