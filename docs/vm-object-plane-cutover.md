# SURU — VM Object-Plane Cutover Design (Stage 11D-BR)

Status: architecture/planning only. No production source changed. Source-grounded;
facts verified against the checkout at `428a9af` (branch `stage11-vm-objmodel`).

---

## 1. Verified repository state
- Branch `stage11-vm-objmodel`, HEAD `428a9af`, working tree clean.
- Mainline `fix/collector-lifecycle` @ `eef8999` untouched.
- No production source modifications in this task.

## 2. Source-grounded current runtime
- Value representation: `s_value` union has `u.o` (`s_gcobj*`, legacy) and `u.idx`
  (`s_u64`, handle). `S_VAL_OBJ` is the only object tag.
- Allocation: `vm_alloc` → `s_heap_alloc` (33 sites) returns `s_gcobj*`; values are
  pushed with `s_val_obj(o)` (pointer). `s_heap_new_exception` used for exceptions.
- Access: every object-access site dereferences `u.o` (`s_obj_str_data/len`,
  `s_obj_array_data`, `s_obj_record_fields`, `s_obj_exception_fields`, `u.o->kind`,
  `u.o->count`) in `vm.c`.
- Roots: operand stack `st[0,sp)` via `s_heap_set_stack_root`; C-local temporaries via
  14 `s_heap_add_root/remove_root` pairs + `MKCLOSURE_V` env; no mutable globals.
- GC triggers: `maybe_collect` inside `vm_alloc` (full/incremental).
- Handle boundary: `S_VAL_OBJ` can carry a full 64-bit handle (`u.idx`) with explicit
  constructors/accessors (`value.h`), but live allocations do not yet produce handles.
- New modules: `objmodel` (alloc/validate/inspect/read/write), `objgc` (mark-sweep,
  incremental, stack/svalue roots, STW lock), `vmobj` (narrow wrapper), `s_vm_gc_bind/
  sync` (Stage 11C).

## 3. Object-family inventory
| Family | Legacy allocation | Current representation | Access ops | Nested refs | Root needs |
|---|---|---|---|---|---|
| String | `s_heap_alloc(STR, len, len)` | `s_gcobj*` | `s_obj_str_data/len`, idx-get | none | stack |
| Array | `s_heap_alloc(ARRAY, n, 0)` | `s_gcobj*` | `s_obj_array_data`, idx ld/st | child `s_value` | stack |
| Record | `s_heap_alloc(RECORD, n, 0)` | `s_gcobj*` | `s_obj_record_fields`, field ld/st | child `s_value` | stack |
| Closure | `s_heap_alloc(CLOSURE, k, 0)` | `s_gcobj*` | child slots (`s_obj_array_data`) | captured values | stack + env temp |
| Exception | `s_heap_new_exception` | `s_gcobj*` | `s_obj_exception_fields` | type/message/cause | stack |
| Module const strings | C-owned `m->strings` | `char*` | memcmp | none | none (unmanaged) |

## 4. Architectural alternatives

### Option A — Atomic object-plane cutover (recommended)
Allocation and representation change together in the new execution path:
- New path allocates through `objmodel`; `s_val_obj_h(handle)` pushed; `S_VAL_OBJ`
  carries `u.idx`; every access resolves via `vmobj` (inspect/s_om_read/write).
- Object-internal references store full handles (`u.idx`), never raw pointers.
- Legacy path preserved as a separate execution mode (separate build/runtime selection),
  not a dual representation inside one VM.
- Pros: unambiguous ownership, one representation per execution path, no pointer/handle
  confusion, matches the "object identity = lifetime" principle, simplest to make safe.
- Cons: largest single-step code change at the cutover point (allocation+access together).

### Option B — Explicit dual-representation migration
A transitional VM holding both legacy-pointer objects and new-handle objects.
- Required: unambiguous representation discriminator, ownership/dispatch for allocation,
  access, destruction, cross-representation references, GC, roots, diagnostics, rollback,
  and rules preventing pointer/handle confusion. A runtime flag or bit-pattern sniff is
  not sufficient (values are copied raw; a flag is process-global and cannot tell two
  representations apart per value; bit inspection is not robust).
- Cross-representation references inside payloads are the fatal complication: a record
  could store a legacy pointer child and a handle child with no safe way to know which,
  and two collectors would need coordinated traversal of one graph.
- Verdict: safe dual representation cannot be proven with the existing `s_value`
  payload layout; reject Option B.

## 5. Recommended architecture
**Option A — atomic object-plane cutover**, delivered as ordered milestones (see §8),
with the legacy path kept intact as a separate execution mode until parity is proven.
Each migrated family moves wholly (allocation+representation+access+roots); no family is
left half-migrated; no object crosses heap ownership.

## 6. Complete object-plane contract
| # | Operation | Responsible module / API |
|---|---|---|
| 1 | Allocation | `objmodel` `s_om_alloc` (via `vmobj` `s_vmobj_alloc`) |
| 2 | Handle creation | `s_val_obj_h`/`s_val_obj_h_parts` (`value.h`) |
| 3 | Value construction | `S_VAL_OBJ` carries full `u.idx` |
| 4 | Stack propagation | `s_value` copy (64-bit intact) |
| 5 | Argument propagation | `s_value` by value (copy) |
| 6 | Return value | `s_value` by value |
| 7 | Field storage | store full handle in child `s_value` (`u.idx`) |
| 8 | Nested refs | full handles only, never raw pointers |
| 9 | Object reads | `vmobj` `s_vmobj_read` / inspect view |
| 10 | Mutation | `vmobj` `s_vmobj_write` (bounds-checked) |
| 11 | Root registration | `s_vm_gc_sync` + `s_om_gc_set_svalue_root` |
| 12 | C-local temps | `s_vmobj_temp_root`/`temp_unroot` scoped |
| 13 | Collection safety | `objgc` STW lock; collect only at `vm_alloc` boundary |
| 14 | Invalidation | free clears slot magic (stale immediately) |
| 15 | Stale rejection | `s_om_validate` generation+slot match |
| 16 | Destruction/reuse | `objmodel` free + generation advance |

Lifetime of `vmobj` pointers (`s_vmobj_inspect`): stable only until the next collection /
allocation that may reclaim the object; valid only outside an active collection and while
the object is live. Access must be revalidated across any allocation/collection boundary.
Prohibited during active collection: allocation, free, payload writes (enforced by
`S_OM_ECOLLECT`), and root/svalue changes.

## 7. Legacy compatibility strategy
- **Decision (approved): C2 — big-bang cutover.** The new handle VM replaces the pointer
  VM directly; the legacy runtime is retained only as historical git reference, not as a
  live selectable mode. `src/mem.c` remains compiled until retirement review but is not a
  differential baseline.
- A single, coherent `S_VAL_OBJ` handle representation is used throughout; no dual mode,
  no discriminator.
- Differential (T7) compares the new runtime against the pre-cutover legacy behavior as a
  frozen snapshot (git baseline), not a live sibling runtime.
- Rollback: the pre-cutover commit (git) is the restore point; `src/mem.c` is not removed
  before the retirement assessment.

## 8. Dependency-ordered implementation roadmap
Milestones (renumbered to avoid collision/ the 11D-B; map to the original plan):
1. **11D-B1 — allocation cutover** (produce handles at all object allocation sites, use
   `s_val_obj_h`; no access conversion yet beyond keeping legacy-identical behavior by
   resolving handles back only in the legacy backend — if infeasible, gate by mode).
2. **11D-C — object-access conversion** (migrate access sites per family through
   `vmobj`; family-ordered: strings → arrays → records → closures → exceptions).
3. **11D-D — roots/temp-reference correctness** (route all C-local temps + `MKCLOSURE_V`
   through `s_vmobj_temp_root`; prove survival/reclamation).
4. **11D-E — collector integration** (drive `objgc` from `vm_alloc`/`maybe_collect`;
   add `!s_om_is_collecting` guards on array/record stores; enable STW).
5. **11D-F — mode selection + differential** (legacy vs new execution selection; program
   corpus comparison).
6. **11D-G — self-hosting parity** (byte-for-byte rebuild gate) + stress/fuzz/sanitizer.
7. **11D-H — legacy retirement assessment** (separate proposal; evidence-based).

Acceptance per milestone: focused tests + full gates green; stop conditions: any parity
failure, sanitizer finding, or unproven root path halts the migration.

## 9. Safety guarantees
- No pointer-to-handle reinterpretation; no handle truncation; no stale-handle acceptance
  (validate generation+slot); no ambiguous ownership; no unchecked access; no hidden raw
  pointers in payloads; no collection while required references unprotected; no mutation
  during collection; no premature legacy retirement.
- C-local temporaries are explicitly rooted; the operand-stack root alone is not treated
  as sufficient until temp safety is proven per site.

## 10. Verification strategy
- Now: existing legacy tests stay green; reference model 34/34; ASan+UBSan; analyze no new
  warnings; focused `vmobj`/value/root tests.
- Later (per milestone): handle-based execution tests; differential program corpus; failure
  injection (allocation/collection); fuzz on handle/access boundaries; self-host
  byte-for-byte rebuild. Only run checks are reported as passing.

## 11. Documentation changes
- This design document (`docs/vm-object-plane-cutover.md`).
- Migration plan updated to reference this design and the renumbered milestones.

## 12. Explicit answers
- **Can allocation and access be migrated independently?** No — source shows access
  follows representation; independent migration yields incoherent/mixed state.
- **Should the new runtime use handles exclusively?** Yes, within the new execution path
  (Option A). Legacy path remains separate.
- **Can legacy pointers and new handles coexist safely?** Not with the current `s_value`
  payload layout; reject dual representation without a proven discriminator.
- **How are C-local temporaries protected?** Explicit scoped temp-roots
  (`s_vmobj_temp_root`/`temp_unroot`) on all exit paths; audited per site (the 14
  `add_root` sites + `MKCLOSURE_V`).
- **First point the new collector can be activated?** After allocation cutover, access
  conversion, root/temp correctness, and STW guards are in place and green (11D-E),
  gated by differential + self-host parity.
- **Evidence required before `src/mem.c` retirement?** No production path depends on the
  legacy collector in new mode; all families migrated; VM-level reachability/reclamation
  tests pass; differential parity; self-host byte-for-byte; no unresolved sanitizer
  findings. Then a separate retirement proposal.

## Missing evidence / open question
The atomic cutover's exact safe sequencing of the allocation step (keeping the legacy
backend runnable at that point) depends on whether a mode gate can be introduced without a
transient mixed state. This is resolved at milestone 11D-B1 implementation time against
the concrete code, not guessed here.
