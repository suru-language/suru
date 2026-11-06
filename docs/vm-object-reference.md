# SURU VM Object-Reference Inventory (Stage 11D-0)

Audit of every object-related operation, reference location and collection trigger in
the current (legacy) VM. Source of truth: `src/vm.c`, `src/runtime.c`, `src/stdlib.c`,
`src/mem.c`, `include/suru/{value.h,mem.h}`. The legacy heap (`s_heap`) remains the
active allocator until the object-plane migration (Stage 11D) flips to `objmodel`.

## 1. Object representation
- `s_value` union holds an object reference as:
  - `u.o` = `s_gcobj*` (legacy pointer) for `S_VAL_OBJ`, or
  - `u.idx` = `s_om_ref` (64-bit generation-protected handle) under the migration.
- Tags: `S_VAL_OBJ` is the only object tag; immediates are `INT/FLOAT/BOOL/NONE`.

## 2. Allocation points
All allocation routes through `vm_alloc` → `s_heap_alloc` (33 call sites) except:
- `s_heap_new_exception` (`BC_NEW_EXCEPTION`, vm.c:1812) which calls `s_heap_alloc`
  directly (bypasses `maybe_collect`; relies on mem.c-internal trigger).

`vm_alloc` calls `maybe_collect` before allocating; `maybe_collect` runs full or
incremental collection and re-registers the stack root each time.

## 3. Object access / mutation sites (`vm.c`)
| Family | Access pattern | Representative sites |
|---|---|---|
| STR | `s_obj_str_data(o)`, `s_obj_str_len(o)` | ~80 sites (bytes, compare, substr, split, join, pad, etc.) |
| ARRAY | `s_obj_array_data(o)` | idx/arr-len/slice/make/concat/sort |
| RECORD | `s_obj_record_fields(o)` | field load/store |
| CLOSURE | child slots (`s_obj_array_data`) | `BC_MKCLOSURE(_V)` |
| EXCEPTION | `s_obj_exception_fields(o)` | `BC_NEW_EXCEPTION`, uncaught/matches |
| generic kind | `v.u.o->kind` | type_of/to_str/len dispatch (vm.c:633) |

Mutation: `BC_IDX_ST` and `BC_FIELD_ST` write child values via `s_heap_write_barrier`
then store; closure capture writes env slots in `BC_MKCLOSURE_V`.

## 4. Roots
- Primary: operand stack `st[0, sp)` — registered via `s_heap_set_stack_root` at each
  collection; includes locals, temporaries, call args/results, exception value during
  unwind. Call frames (`s_cont`) and handler frames (`s_handler`) hold only indices into
  `st` (no separate references). No mutable-global storage exists in this IR.
- C-local temporaries (explicit `s_heap_add_root/remove_root` pairs): vm.c lines 619, 714,
  986, 1047, 1082, 1099, 1136, 1215, 1311, 1332, 1388, 1740, 1962 (array/record
  construction, substr, split, join, pad, concat, closure-env capture).
- These C-local temporaries are the only references not covered by the operand-stack root
  and must be explicitly rooted under any collector.

## 5. Collection triggers
- `maybe_collect` (called from `vm_alloc` before every allocation) — the only explicit
  trigger, and `s_heap_new_exception` (implicit mem.c-internal trigger).
- Collection runs only inside these choke points, so the live set is always exactly
  `st[0, sp)` + rooted C-local temporaries at that moment.

## 6. Migration notes
- Every object-bearing reference resolves either through the stack (bulk root) or through a
  scoped C-local temporary root. No other hidden reference locations exist.
- The `u.o`→`u.idx` transition therefore requires: (a) route allocation to `objmodel`;
  (b) replace all `s_obj_*`/`u.o->kind` access with the narrow VM object-access interface;
  (c) convert the C-local temporary `s_heap_add_root` sites to handle temp-roots;
  (d) drive collection through the bound `objgc` using `s_vm_gc_sync` (Stage 11C).
