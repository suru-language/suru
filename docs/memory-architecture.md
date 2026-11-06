# SURU Memory-Manager Architecture — Phase 3D-1

Status: design / approval document. No production collector port is started by this
document. Baseline verified: branch `fix/collector-lifecycle`, HEAD `aeb5f54`,
clean tree, `make test` exit 0 (91/91 integration, 31/31 self-hosting gates,
12/12 reference-model checks, 34 GC unit checks), self-hosting rebuild
byte-for-byte.

---

## 1. Architecture review

### 1.1 Heap
- Regions: no explicit heap regions in the current C bootstrap; all objects come
  from system `malloc` (then spill into free lists after a sweep). A production
  SURU heap should own an explicit set of raw regions.
- Metadata: `s_heap` holds per-size free buckets (small ≤ 4096 B, 16 B
  bucketing), a large-object free list, an intrusive `all_live` list, live/total
  byte counters, and incremental-marking state.
- Alignment: objects are rounded to 16 bytes; payloads 8-aligned
  (`sizeof(s_value)`).
- Object header (`s_gcobj`): `chunk_size`, `next_all`, `count`, `magic`,
  `kind`, `colour`, `flags`; payload follows the header.
- Free-space management: small objects reuse per-bucket free lists; large
  objects use a single free list keyed by first-word link.
- Allocation failure: `s_heap_alloc` returns `NULL`; fault injection
  (`fail_alloc`/`fail_after`) exists for testing.

### 1.2 Object model
All runtime heap objects are one of: string (byte payload), array / record /
closure / exception (contiguous `s_value` child slots). No environment object
exists as a distinct kind — closures carry captured values in their slot array.

### 1.3 References
- A managed reference is a `s_value` whose `tag == S_VAL_OBJ` and `u.o` points
  to a `s_gcobj`. Immediates are not managed.
- Stale-reference detection is not built into the C collector (relies on the
  tri-color invariant + correct root hygiene). Object identity is address-based.

### 1.4 Roots
- Global roots: none currently (implicit via `all_live` only while reachable).
- VM operand stack: a root range (`stack_base`/`stack_len`).
- Call frames / locals: part of the operand stack range.
- Temporary roots: explicit C-local `s_heap_add_root`/`remove_root` slots (the
  class of defect fixed in `builtin_str_split`/`builtin_substr`).
- Native interface roots: N/A in the C bootstrap; must be defined for the
  platform interface.

### 1.5 Collector
- Tri-color mark-and-sweep; grey worklist emerges a growable `mark_stack`; a
  write barrier exists (`s_heap_write_barrier`) but is only meaningful during
  incremental marking.
- Incremental collection: budget-slice `s_heap_collect_step` +
  `s_heap_collect_incremental`.
- Failure modes: mark-stack growth allocation failure aborts (never silently
  abandons reachable objects). Sweep frees whites into free lists.

### Weaknesses identified
1. No explicit heap-region ownership/lifetime — `malloc`/`free` are scattered.
2. Mark worklist is unbounded and allocated from the managed heap (realloc).
3. Object-graph traversal is recursive in the C `mark_process_one` children loop
   only in the sense of a bounded loop; but the worklist can grow without bound.
4. No stale-reference, corruption, or double-free detection at runtime.
5. No runtime distinction between trusted-runtime and ordinary-program memory.
6. `natural_vm.suru` (the SURU interpreter) has no memory abstraction at all and
   models values as tagged strings.

---

## 2. Approved heap & object model (production target)

- Heap owns a list of raw regions, each with base, length, alignment, and usage
  metadata. Object memory comes from these regions, not from system `malloc`.
- 16-byte object alignment; payloads 8-aligned.
- Header (kept compatible with the current `s_gcobj` wire layout during
  transition): kind, count, size/rounded, magic, colour, flags, live-list link.
- Free-space: small-object buckets + large free list, owned by SURU.
- Object kinds preserved: str / array / record / closure / exception (and an
  explicit environment only if introduced later; closures currently carry
  captured values directly).
- Allocation metadata is checked: overflow-checked size arithmetic, alignment,
  and region bounds.

## 3. Approved reference representation

- Managed references remain `s_value` with a tagged `s_gcobj*`.
- No interior pointers, no weak references, no pointer arithmetic.
- Object identity = the object's address (stable within a heap region).
- Stale-reference detection is achieved by never letting a free-list object stay
  reachable; a debug build can poison freed payloads and validate magic on
  dereference.

## 4. Root-management contract

- Roots are the only entry points into the managed graph.
- Required sets: global roots, VM operand stack, call-frame locals, temporary
  runtime roots, native-interface pins.
- Each `add_root` must have a matching `remove_root` on all paths (normal and
  error). The runtime uses single-exit cleanup.
- A root is valid only while its `s_value*` points to live storage.
- Roots are re-seeded on every marking slice.
- Temporary-array rooting (the `str_split` pattern) is a first-class contract
  requirement, with a model scenario asserting the invariant.

## 5. Minimal platform interface

SURU lacks raw-memory/pointer/FFI primitives. The smallest controlled interface:

| op | in | out | pre | post | bounds | failure | GC | scope |
|---|---|---|---|---|---|---|---|---|
| acquire_raw(n, align) | int, int | RawBlock id | n>0, align pow2 | new region, zeroed | — | error id | no | trusted |
| release_raw(id) | RawBlock | ok | id valid | region returned | — | error | no | trusted |
| blk_size(id) | RawBlock | int | valid | len | — | error | no | trusted |
| blk_align(id) | RawBlock | int | valid | align | — | error | no | trusted |
| blk_load_u8/u32/u64(id, off) | RawBlock,int | value | valid, bounds | value | checked | trap | no | trusted |
| blk_store_u8/u32/u64(id, off, v) | RawBlock,int,value | ok | valid, bounds | stored | checked | trap | no | trusted |
| blk_validate(id) | RawBlock | bool | — | false if stale | — | false | no | trusted |

- Preconditions: region id valid and not released; offsets within region.
- Ownership: regions owned by the runtime; ordinary programs get a safe
  read-only view only if explicitly exposed.
- These operations never trigger GC and are restricted to trusted runtime code.

## 6. Bootstrap workspace designs

### Design A — Fixed collector workspace
Reserve a fixed (non-managed) region for the mark worklist.
- Max objects / depth bounded by region size.
- Overflow: deterministic — either fall back to a per-root re-seed scan, or
  abort with a diagnostic; never silently abandon reachable objects.

### Design B — Platform-provided raw workspace
Acquire a separate region (e.g., `mmap`) for collector metadata + traversal.
- Pros: size tunable; independent of managed heap; release is clean.
- Cons: still needs the small C adapter (or a native syscall) to obtain it.

### Design C — Intrusive marking metadata
Reuse marking bits in the object header (colour already exists); store worklist
links in-object when not actively traversed, reducing separate allocation.
- Pros: no per-mark allocation; supports incremental marking and interrupted
  marking recovery.
- Cons: header size grows; must be careful with concurrent mutation / write
  barriers.

### Recommendation
Combine B + C + an explicit bounded worklist (A): obtain an **unmanaged raw
workspace** from the platform interface (B) for a **bounded mark stack** with
fixed capacity, and keep mark state in the object header (C). Overflow is
handled deterministically (re-seed loop, otherwise abort with diagnostic). This
is the safest practical design and is what the reference model’s bounded
worklist mirrors.

## 7. Memory-safety invariants (each with a test)

1. Every live managed reference points to an allocated object. — C unit +
   reference model (validate in debug).
2. Every object header describes a valid object. — magic check in debug.
3. Every size calculation is checked (overflow/alignment). — unit.
4. Every reachable-from-root managed reference is preserved. — differential.
5. Every temporary root has a balanced lifetime. — reference scenarios 11–12.
6. Every freed object is unreachable. — differential.
7. Every allocated object is either managed or explicitly outside the heap. — unit.
8. Collector-workspace exhaustion never causes incorrect reclamation. — model
   overflow scenario.
9. Allocation failure is reported deterministically. — `fail_alloc` units.
10. No collector operation recursively invokes managed allocation. — static
    check + runtime guard.

## 8. Reference-model coverage assessment

Covered: reachability, unreachable reclamation, cycles (rooted/unrooted),
multiple roots, root removal, nested graphs, worklist overflow, allocation cap,
unrooted/rooted temporaries.

Missing (recommend as new model + C differential scenarios):
- deep and wide object graphs,
- alias multiplicity (several references to one object),
- mutation during incremental marking,
- write-barrier correctness,
- corrupted/stale object metadata,
- repeated collection under allocation pressure,
- large-object handling,
- root removal mid-collection,
- deterministic OOM/large-region failure.

The reference model is an abstract object-graph model, not a production memory
manager; it only validates traversal/reachability rules.

## 9. C dependency and replacement map

| Component | Temp C | Interface | Replacement | Acceptance |
|---|---|---|---|---|
| Bootstrap compiler | yes | CLI | compiler written in Natural | selfhost-rebuild, selfhost-compare |
| Platform memory acquisition | yes (adapter) | `acquire_raw/release_raw` | native ELF `mmap` syscalls | raw-region unit tests on native backend |
| Native execution interface | C ELF writer | — | SURU ELF writer | `--backend=native` gates |
| Debugging | yes | stderr | optional, may remain | — |

Final target: no C compiler or C runtime required to build/execute ordinary SURU
applications.

## 9.5 Implemented so far — Stage 1–2 (bootstrap adapter)

The trusted platform interface and RawBlock abstraction are implemented behind the
approved API (`include/suru/platform_mem.h`, `src/platform_mem.c`) in the C17
bootstrap. Key properties:

- Opaque `RawBlock` handle (`s_pm_handle`); raw pointers never escape this module.
- `s_pm_acquire(size, align, &h)` / `s_pm_release(h)`, validated handle checks.
- Region queries: size, align, validate; failure codes for invalid/stale handles,
  out-of-bounds access, arithmetic overflow, and allocation failure.
- `s_pm_read`/`s_pm_write` check offsets and lengths before any copy; typed
  u8/u32/u64 accessors are thin wrappers over the checked operations.
- Zero-length regions are valid; length-0 accesses succeed, any non-zero access
  fails. Alignments are power-of-two (0 => default 16). Request sizes are
  overflow-checked before allocation.
- Fault injection (`s_pm_set_fail_alloc`, `s_pm_set_alloc_fail_after`) and
  leak/test helpers (`s_pm_active_count`, `s_pm_dispose_all`) are provided.
- This module never triggers garbage collection and is **trusted-runtime-only**:
  no SURU language primitive exposes raw memory or RawBlock handles yet.

Tests (tests/unit/test_platform_mem.c, 474 checks): acquire/release, zero-size,
allocation failure + fail-after, invalid/stale handles, double release, bounds
reads/writes (start and final byte), OOB read/write, offset/length overflow,
invalid alignment, repeated cycles, and cleanup/leak. ASan+UBSan clean.

Stage 3 will build a SURU-owned object model and allocator on top of this
interface. Ordinary SURU programs will only see an approved, checked surface
that the trusted runtime exposes for them, never raw RawBlock handles.

## 9.6 Phase 3E — SURU-owned object model & allocation architecture (design)

This section records the Phase 3E design for a SURU-owned object model and managed
allocation on top of the Stage 1–2 platform interface. It extends the approved
Sections 2–5 (heap / object / reference / root / platform) without replacing them.
It is architecture-only: no production heap/GC code is written in this phase. At
approval the baseline stays clean at HEAD `7069f2f` (91/91 integration, 31/31
gates, 12/12 reference model, 34 GC units, 482 platform units).

### D1 — Object representation (SURU-owned)
- An object is a fixed-format header at a known offset inside a managed region
  (one region per heap, or a set of regions). Object identity is a **flat object
  index** (u32) into the heap, not a native address.
- Header: `kind` (u8), `flags` (u8), `colour` (u8), `size/rounded` (u32),
  `magic` (u32), `count` (u32), `next_free`/linkage as needed. Payload follows
  the header, 8-aligned; overall 16-byte alignment. This maps onto the existing
  `s_gcobj` layout.
- Kinds preserved: str / array / record / closure / exception. Closure carries
  captured values in its child-slot array (no separate environment kind yet).
- Keep the **wire-compatible** mapping to the existing `s_gcobj` during the
  bootstrap transition, but express identity as index, not pointer.

### D2 — Reference representation
- A managed reference is a tagged value carrying a **heap index** (opaque), not
  a raw pointer: `S_VAL_OBJ` with `u.idx` (u32) instead of `u.o` pointer in the
  SURU-owned representation.
- Validation: an index is valid iff it is < heap object count and its header
  magic matches. Stale/dup/invalid indices are rejected by the lookup.
- Object identity = heap index. This permits future relocation (compaction)
  without invalidating externally visible handles (only the runtime layer sees
  addresses).
- Ordinary SURU programs never obtain raw addresses; they only use the checked
  runtime surface.
- **Future transition (not this phase):** the pointer-based `S_VAL_OBJ`
  (`u.o`) → index-based (`u.idx`) change touches the VM/bytecode value layer
  across `include/suru/value.h` and every consumer of `u.o`. That is a separate,
  later approved transition; this phase commits only the index-based identity and
  validation semantics documented here.

### D3 — Allocation responsibilities (separation)
- SURU-controlled: object layout, metadata, validation, allocation policy,
  free-space tracking, registration, bookkeeping, canary/poison in debug.
- Trusted platform (`platform_mem`): raw acquire/release, checked low-level
  access only.
- The C adapter is replaceable without redesigning the object model: the model
  only asks the platform for RawBlock regions plus checked byte access.

### D4 — Allocation algorithm (design only)
- Evaluate bump + free-list and segregated small-bucket allocator; recommend a
  segregated small-bucket freelist (aligned chunks) with a bump allocation for
  the first version, because it is simple, checkable, and matches the existing C
  small-bucket scheme. Do not implement now.
- OOM/failure path: deterministic error return, no partial object registration,
  never corrupt existing objects.

### D5 — Bootstrap initialization (no self-dependency)
- The first SURU-owned allocator initializes from one or more RawBlocks acquired
  via `s_pm_acquire`. It must not use the managed heap to build its own metadata:
  metadata (free lists, header slots) lives inside the RawBlock region or in
  dedicated RawBlocks acquired for bookkeeping. This keeps the collector/allocator
  non-recursive.
- Accept: the initial allocator (and the VM bridge) may remain C-hosted for now;
  only object layout/policy are SURU-authored in this phase.

### D6 — Safety invariants (each mapped to a future test)
1. Valid reference → allocated object with valid magic.
2. Header describes valid object (magic, size, kind).
3. Size/alignment valid; object fits inside its region.
4. Allocation arithmetic overflow-checked.
5. Metadata cannot silently diverge (debug canary/poison).
6. Invalid/stale references rejected.
7. Allocation failure leaves existing objects intact.
8. Free-space bookkeeping never overlaps live objects.
9. No allocation path silently loses ownership.
10. Object-model initialization does not require recursive managed allocation.

### D7 — Reference-model test gaps to plan (not implement here)
Future additions to `selfhost/memory/ref_gc.suru` (append as scenario 13+; its
current numbering skips 10):
- deep/wide graphs, alias multiplicity, mutation during marking, write-barrier
  correctness, stale/corrupt metadata, repeated collection under pressure, large
  objects, root removal mid-collection, OOM and alignment/overflow of offsets.

### Staged roadmap
This phase commits only the architecture. Production implementation begins only
after approval of this section:
object layout → reference representation → metadata → allocator initialization →
bookkeeping → free-space → validation → VM integration → roots → marking/collection
→ reclamation → replace the C collector. Each stage has tests.

## 10. Implementation roadmap (each stage has tests)

1. Minimal trusted platform interface (C adapter, tests).
2. Raw memory-region abstraction (RawBlock, tests).
3. SURU object metadata.
4. SURU allocation.
5. Root registration.
6. Object traversal.
7. Marking.
8. Sweeping.
9. Free-space reuse.
10. Allocation-pressure handling.
11. Incremental collection if justified.
12. SURU VM integration.
13. Native platform implementation.
14. Remove the C collector from normal execution.

## 11. First production component (recommendation)

Implement **Stage 1 + Stage 2 first**: the minimal trusted platform interface and
the raw memory-region abstraction, as an isolated module with its own tests
(bounds, alignment, failure, lifetime). Everything else (heap, objects,
collector, VM integration) builds on these, and they are the smallest components
that can be owned and tested independently today without a managed heap.
