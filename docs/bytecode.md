# SURU Bytecode Format

This document specifies the SURU bytecode format and instruction set.

## 1. Module layout (serialized)

The serialized form is little-endian and architecture-independent. Host types
never leak into the format.

| field | type | notes |
|-------|------|-------|
| magic | 4 bytes | `"SURU"` |
| version | u32 | must equal 1 |
| entry | u32 | index of the `main` function |
| nints | u32 | |
| nfloats | u32 | |
| nstrings | u32 | |
| nrecords | u32 | |
| nfuncs | u32 | |
| int table | nints × i64 | |
| float table | nfloats × f64 | |
| string table | nstrings × (u32 length + bytes) | |
| record table | nrecords × u32 field-count | |
| function table | nfuncs × function record | |

A function record is: u32 name length + bytes, u32 nparams, u32 frame_size,
u32 max_operand_depth, u32 nins, then nins instructions. Each instruction is a
1-byte opcode plus three u32 operands (`a`, `b`, `c`).

## 2. Instructions

The full opcode table is defined in `include/suru/bc.h` (`s_bc_op`) with its
stack-effect metadata in `src/bc.c` (`s_bc_op_table`). Key families:

- Constants: `const_int`, `const_float`, `const_str`, `const_none`
- Locals: `ld_local`, `st_local`
- Stack: `pop`, `dup`, `swap`
- Unary: `not`, `neg_i`, `neg_f`
- Binary: int/float arithmetic and comparisons; string comparisons; bool eq
- Objects: `new_array`, `new_record`, `idx_ld`, `idx_st`, `arr_len`,
  `field_ld`, `field_st`
- Control: `call`, `builtin`, `ret`, `ret_unit`, `jmp`, `jz`, `jnz`,
  `trap`

## 3. Verification guarantees

The verifier (`src/verify.c`) guarantees, before any execution:

- every opcode and operand is valid and in range (constants, locals, fields,
  functions, builtins, jump targets);
- the operand stack never underflows and never exceeds the declared maximum
  depth on any reachable path;
- every reachable path terminates in a return or trap;
- branch target merge depths agree.

The VM refuses to run a module that has not passed verification.
