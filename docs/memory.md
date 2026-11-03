# SURU Memory Management Model

This document specifies the memory-management model of the SURU runtime as
actually implemented.

## 1. Overview

SURU has two memory domains:

1. **Compiler arena.** The compiler frontend allocates tokens, AST nodes, and
   symbol tables in an arena that is released in bulk when compilation of a
   module completes. Arena allocations never shrink individually; the entire
   arena is freed at once.
2. **Managed heap.** Runtime objects (`str`, arrays, records) live on a
   managed heap controlled by a tracing garbage collector.

There is no user-visible manual deallocation. The language offers no `free`
and no raw pointers.

## 2. Value representation

Runtime values are represented by a tagged union `Value`:

```
Value {
    s_value_tag tag;   // int, float, bool, none, obj
    union {
        s_i64 i;        // int
        double f;        // float
        bool b;          // bool
        s_gcobj *o;      // object pointer
    } u;
}
```

Immediates (`int`, `float`, `bool`, `none`) are stored directly. Object values
store a GC object pointer. Boolean values are represented at runtime as the
integers 0 and 1; the `bool` distinction is enforced statically by the
frontend.

## 3. GC objects

Every heap object begins with a header:

```
struct s_gcobj {
    s_size  chunk_size;   // total allocated bytes, incl. header
    s_gcobj *next_all;    // intrusive link in the live-object list
    s_u32   count;        // str: byte length; array/record: child Value count
    s_u32   magic;        // sanity tag (corruption detection)
    s_u8    kind;         // str | array | record | closure
    s_u8    marked;       // mark bit
    s_u16   flags;
};
```

The payload follows the header and is zero-filled at allocation. For `str`,
the payload is `count` raw bytes. For `array` and `record`, the payload is
`count` `Value` slots. The collector therefore never scans uninitialized
memory: it walks exactly `count` slots of an object whose kind has children,
and no slots for strings.

## 4. The allocator

The managed heap is a custom allocator:

- **Small objects** (rounded to 16-byte buckets up to 4096 bytes) are served
  from segregated free lists, one bucket per size class. Free chunks are
  singly linked through their first word.
- **Large objects** come from a large-object free list; if none fits, a fresh
  chunk is requested from the system allocator.
- Every live object is linked into an intrusive **all-live** list used by the
  collector.

## 5. The collector

The collector is a **non-moving, stop-the-world mark-sweep** tracer:

1. **Roots** are the VM's live stack region (registered as a `(base,len)`
   range) plus any individually registered root slots.
2. **Mark.** All live objects are unmarked, then roots are scanned. Reachable
   objects are marked using an explicit worklist; child `Value` slots are
   followed for arrays/records. A header magic check aborts with a clear
   message if a corrupted pointer is ever followed.
3. **Sweep.** Unmarked objects are unlinked from the all-live list and their
   chunks are returned to the appropriate free list (or freed for large
   chunks). The collector is non-moving, so object addresses are stable.

The collector runs automatically in the VM — the allocation counter or the
live-bytes threshold is crossed, and can be invoked explicitly (used by
tests). Because no object is moved, no forwarding pointers are required.

The collection trigger uses an adaptive sizing heuristic (nursery sizing):
after each collection the live-bytes threshold is resized to leave a fixed
headroom (default 4x) over the surviving set, clamped to a floor (the initial
threshold, overridable via `SURU_GC_THRESHOLD`) and a ceiling. When survival is
tiny, the threshold stays at the floor and collections stay frequent and
cheap; when the live set grows, the threshold grows with it so collection
frequency tracks real allocation/survival instead of growing without bound.
The threshold is never shrunk while the heap is still mostly live (to avoid
thrashing).

### 5.1 Tri-color marking and incremental collection

The collector marks with the standard tri-color scheme: white (unreached),
grey (discovered, on the worklist), black (scanned). A budgeted primitive
`s_heap_collect_step(heap, budget)` scans up to `budget` grey objects and
returns the number still queued, so marking can be performed incrementally and
interleaved with allocation.

Incremental mode (`SURU_GC_INCREMENTAL=1`) drives collection this way:

- When the trigger fires, a marking cycle starts and runs in slices of
  `VM_GC_INCREMENTAL_BUDGET` grey objects per allocation interval.
- A **write barrier** at array/record stores shades a white target grey when
  it is written into a black object, preserving the tri-color invariant.
- Objects allocated while a cycle is active are shaded grey immediately so
  they are scanned, and roots are re-seeded each slice so values newly
  referenced from the stack are not missed.
- When marking completes, the remaining white objects are swept.

The default (unset) behavior is a full stop-the-world collect that drains
marking and sweeps in one call; the two are equivalent in the objects they
retain.

## 6. Allocation failure

All allocation goes through `s_heap_alloc`. On failure it returns `NULL`; the
VM turns this into a controlled out-of-memory trap. The allocator never calls
`abort` itself. A **fault-injection** hook (`s_heap_set_fail_alloc`,
`s_heap_set_alloc_fail_after`) forces allocation failures so the OOM path is
testable.

## 7. Testing the model

The test suite exercises:

- allocation failure (via the fault-injection hook),
- object lifetimes across collection cycles,
- cyclic references (records that reference each other),
- garbage-collection correctness (reachability after collection),
- deep shared-reference graphs and reference rewiring across many frequent
  collections (`gcstress2`/`gcstress3`, run under a tiny trigger threshold),
- invalid references (via the verifier and runtime traps), and
- cleanup of arenas and the heap at teardown.

## 8. Safety properties

- No uninitialized heap bytes are scanned (metadata-driven traversal).
- Out-of-bounds object reads are prevented by bounds checks in the VM.
- The collector is only invoked at instruction boundaries, when the stack root
  range is current.
- The VM refuses to execute unverified bytecode, and the bytecode verifier
  guarantees the operand-stack discipline that keeps the runtime stack safe.
