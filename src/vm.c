#include "suru/vm.h"
#include "suru/verify.h"
#include "suru/stdlib.h"
#include "suru/runtime.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define VM_INIT_STACK 4096u
#define VM_MAX_STACK (1u << 22)
#define VM_GC_INIT_THRESHOLD (1u << 20)
#define VM_GC_ALLOC_INTERVAL 512u
#define VM_GC_MAX_THRESHOLD ((s_u64)1 << 40)
#define VM_GC_HEADROOM 4u
#define VM_GC_INCREMENTAL_BUDGET 16u

typedef struct s_cont {
    s_u32 pc;
    s_size fp;
    s_u32 fid;
    s_size ret_slot;   /* caller operand slot where the result must be written */
} s_cont;

typedef struct s_handler {
    s_u32 fid;
    s_u32 target;
    s_u32 type_index;
    s_size fp;
    s_size sp;
    s_size call_depth;
} s_handler;

struct s_vm {
    const s_bc_module *m;
    s_heap *heap;
    s_value *st;
    s_size st_cap;
    s_size sp;
    s_size fp;
    s_u32 cur_fid;
    bool exit_halt;
    int argc;
    char **argv;
    s_cont *calls;
    s_size ncalls, capcalls;
    s_handler *handlers;
    s_size nhandlers, caphandlers;
    s_u32 alloc_counter;
    s_u64 gc_threshold;
    s_u64 gc_min_threshold;
    bool gc_incremental;
    bool trapped;
    char err[512];
    int exit_code;
};

s_vm *s_vm_create(const s_bc_module *m) {
    s_vm *vm = calloc(1, sizeof(s_vm));
    if (!vm) return NULL;
    vm->m = m;
    vm->st = malloc(sizeof(s_value) * VM_INIT_STACK);
    if (!vm->st) { free(vm); return NULL; }
    vm->st_cap = VM_INIT_STACK;
    vm->gc_threshold = VM_GC_INIT_THRESHOLD;
    const char *gs = getenv("SURU_GC_THRESHOLD");
    if (gs && *gs) {
        char *end;
        unsigned long v = strtoul(gs, &end, 10);
        if (end != gs && v > 0) vm->gc_threshold = (s_u64)v;
    }
    vm->gc_min_threshold = vm->gc_threshold;
    vm->gc_incremental = getenv("SURU_GC_INCREMENTAL") != NULL;
    s_heap_set_incremental(vm->heap, vm->gc_incremental);
    return vm;
}

void s_vm_destroy(s_vm *vm) {
    if (!vm) return;
    free(vm->st);
    free(vm->calls);
    free(vm->handlers);
    free(vm);
}

static void vm_trap(s_vm *vm, const char *fmt, ...) {
    if (vm->trapped) return;
    vm->trapped = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vm->err, sizeof(vm->err), fmt, ap);
    va_end(ap);
    fprintf(stderr, "SURU runtime error: %s\n", vm->err);
}

void s_vm_set_args(s_vm *vm, int argc, char **argv) {
    if (!vm) return;
    vm->argc = argc;
    vm->argv = argv;
}

bool s_vm_trapped(const s_vm *vm) { return vm->trapped; }
const char *s_vm_error(const s_vm *vm) { return vm->err; }

static bool grow_stack(s_vm *vm, s_size need) {
    if (need <= vm->st_cap) return true;
    if (need > VM_MAX_STACK) return false;
    s_size ncap = vm->st_cap;
    while (ncap < need) ncap *= 2;
    if (ncap > VM_MAX_STACK) ncap = VM_MAX_STACK;
    s_value *n = realloc(vm->st, sizeof(s_value) * ncap);
    if (!n) return false;
    vm->st = n;
    vm->st_cap = ncap;
    return true;
}

static bool grow_calls(s_vm *vm) {
    if (vm->ncalls < vm->capcalls) return true;
    s_size ncap = vm->capcalls ? vm->capcalls * 2 : 64;
    s_cont *n = realloc(vm->calls, sizeof(s_cont) * ncap);
    if (!n) return false;
    vm->calls = n;
    vm->capcalls = ncap;
    return true;
}

static bool push(s_vm *vm, s_value v);

static bool grow_handlers(s_vm *vm) {
    if (vm->nhandlers < vm->caphandlers) return true;
    s_size ncap = vm->caphandlers ? vm->caphandlers * 2 : 32;
    s_handler *n = realloc(vm->handlers, sizeof(*n) * ncap);
    if (!n) return false;
    vm->handlers = n;
    vm->caphandlers = ncap;
    return true;
}

static void discard_frame_handlers(s_vm *vm) {
    while (vm->nhandlers > 0) {
        s_handler *h = &vm->handlers[vm->nhandlers - 1];
        if (h->fid != vm->cur_fid || h->call_depth != vm->ncalls) break;
        vm->nhandlers--;
    }
}

static bool exception_matches(const s_vm *vm, s_value exception,
                              s_u32 type_index) {
    if (exception.tag != S_VAL_OBJ ||
        exception.u.o->kind != S_OBJ_EXCEPTION) return false;
    if (type_index == UINT32_MAX) return true;
    s_value type = s_obj_exception_fields(exception.u.o)[0];
    if (type.tag != S_VAL_OBJ || type.u.o->kind != S_OBJ_STR) return false;
    s_size want_len = vm->m->strlens[type_index];
    return s_obj_str_len(type.u.o) == want_len &&
           memcmp(s_obj_str_data(type.u.o), vm->m->strings[type_index], want_len) == 0;
}

static void vm_uncaught_exception(s_vm *vm, s_value exception) {
    if (exception.tag == S_VAL_OBJ && exception.u.o->kind == S_OBJ_EXCEPTION) {
        s_value *fields = s_obj_exception_fields(exception.u.o);
        if (fields[0].tag == S_VAL_OBJ && fields[1].tag == S_VAL_OBJ &&
            fields[0].u.o->kind == S_OBJ_STR && fields[1].u.o->kind == S_OBJ_STR) {
            vm_trap(vm, "uncaught %.*s: %.*s",
                    (int)s_obj_str_len(fields[0].u.o), s_obj_str_data(fields[0].u.o),
                    (int)s_obj_str_len(fields[1].u.o), s_obj_str_data(fields[1].u.o));
            return;
        }
    }
    vm_trap(vm, "throw expects an exception object");
}

static bool vm_throw(s_vm *vm, s_value exception, const s_bc_module *m,
                     const s_bc_func **f, s_u32 *pc) {
    s_size found = vm->nhandlers;
    while (found > 0) {
        --found;
        if (exception_matches(vm, exception, vm->handlers[found].type_index)) break;
    }
    if (found == vm->nhandlers) {
        vm_uncaught_exception(vm, exception);
        return false;
    }
    s_handler handler = vm->handlers[found];
    vm->nhandlers = found;
    vm->ncalls = handler.call_depth;
    vm->cur_fid = handler.fid;
    vm->fp = handler.fp;
    vm->sp = handler.sp;
    *f = &m->funcs[handler.fid];
    if (!push(vm, exception)) return false;
    *pc = handler.target;
    return true;
}

static void maybe_collect(s_vm *vm) {
    vm->alloc_counter++;
    bool should = vm->alloc_counter >= VM_GC_ALLOC_INTERVAL ||
                  s_heap_live_bytes(vm->heap) > vm->gc_threshold;
    if (!should) return;
    s_heap_set_stack_root(vm->heap, vm->st, vm->sp);
    s_u64 before = s_heap_live_bytes(vm->heap);

    if (vm->gc_incremental) {
        /* Incremental collection: run a slice of marking. If the current
         * cycle is not finished, keep the alloc counter high so the next
         * allocation re-enters and continues making progress. */
        if (!s_heap_collect_incremental(vm->heap, VM_GC_INCREMENTAL_BUDGET)) {
            vm->alloc_counter = VM_GC_ALLOC_INTERVAL;
            return;
        }
    } else {
        s_heap_collect(vm->heap);
    }

    s_u64 after = s_heap_live_bytes(vm->heap);
    /* Nursery sizing heuristic: size the next trigger to leave HEADROOM
     * over the surviving set, so collection frequency tracks actual
     * allocation/survival instead of growing without bound. Clamp to
     * [min, max], and do not shrink while the heap is still mostly live
     * (would thrash). */
    s_u64 target;
    if (after > VM_GC_MAX_THRESHOLD / VM_GC_HEADROOM) {
        target = VM_GC_MAX_THRESHOLD;
    } else {
        target = after * VM_GC_HEADROOM;
        if (target < vm->gc_min_threshold) target = vm->gc_min_threshold;
        if (target > VM_GC_MAX_THRESHOLD) target = VM_GC_MAX_THRESHOLD;
    }
    if (target < vm->gc_threshold &&
        (after > VM_GC_MAX_THRESHOLD / 2 || before <= after * 2))
        target = vm->gc_threshold;
    vm->gc_threshold = target;
    vm->alloc_counter = 0;
    if (getenv("SURU_GC_DEBUG")) {
        fprintf(stderr, "GC[%llu] live %llu -> %llu, threshold %llu\n",
                (unsigned long long)s_heap_collections(vm->heap),
                (unsigned long long)before, (unsigned long long)after,
                (unsigned long long)vm->gc_threshold);
    }
}

static s_gcobj *vm_alloc(s_vm *vm, s_obj_kind kind, s_u32 count, s_size payload) {
    maybe_collect(vm);
    s_gcobj *o = s_heap_alloc(vm->heap, kind, count, payload);
    if (!o) {
        vm_trap(vm, "out of memory");
        return NULL;
    }
    return o;
}

static s_value pop(s_vm *vm) {
    return vm->st[--vm->sp];
}

static bool push(s_vm *vm, s_value v) {
    if (!grow_stack(vm, vm->sp + 1)) {
        vm_trap(vm, "operand stack overflow");
        return false;
    }
    vm->st[vm->sp++] = v;
    return true;
}

/* ------------------------------------------------------------------ */
/* Builtins                                                            */
/* ------------------------------------------------------------------ */

/* Format a scalar value into a string for print/println. Returns false for
 * values that have no textual form (raw arrays/records). For a string value,
 * *data points into the GC object; for scalars it points into buf. */
typedef struct s_buf {
    char *data;
    s_size len, cap;
} s_buf;

static bool buf_reserve(s_buf *b, s_size extra) {
    if (b->len + extra + 1 <= b->cap) return true;
    s_size ncap = b->cap ? b->cap * 2 : 64;
    while (ncap < b->len + extra + 1) ncap *= 2;
    char *n = realloc(b->data, ncap);
    if (!n) return false;
    b->data = n;
    b->cap = ncap;
    return true;
}

static bool buf_app(s_buf *b, const char *s, s_size len) {
    if (!buf_reserve(b, len)) return false;
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = '\0';
    return true;
}

/* Recursively render any value into a string. depth guards against cyclic
 * array/record graphs. */
static bool fmt_into(s_vm *vm, s_value v, s_buf *b, int depth) {
    (void)vm;
    if (depth > 32) return buf_app(b, "...", 3);
    switch (v.tag) {
    case S_VAL_INT: {
        char t[40];
        s_size n = s_rt_format_i64(t, sizeof t, v.u.i);
        return buf_app(b, t, n);
    }
    case S_VAL_FLOAT: {
        char t[64];
        s_size n = s_rt_format_f64(t, sizeof t, v.u.f);
        if (n >= sizeof t) n = sizeof t - 1;
        return buf_app(b, t, n);
    }
    case S_VAL_BOOL:
        return buf_app(b, v.u.b ? "true" : "false", v.u.b ? 4 : 5);
    case S_VAL_NONE:
        return buf_app(b, "unit", 4);
    case S_VAL_OBJ: {
        s_gcobj *o = v.u.o;
        if (o->kind == S_OBJ_STR)
            return buf_app(b, s_obj_str_data(o), s_obj_str_len(o));
        if (o->kind == S_OBJ_CLOSURE)
            return buf_app(b, "<closure>", 9);
        if (o->kind == S_OBJ_EXCEPTION)
            return buf_app(b, "<exception>", 11);
        if (o->kind == S_OBJ_ARRAY) {
            if (!buf_app(b, "[", 1)) return false;
            s_value *el = s_obj_array_data(o);
            for (s_u32 i = 0; i < o->count; ++i) {
                if (i && !buf_app(b, ", ", 2)) return false;
                if (!fmt_into(vm, el[i], b, depth + 1)) return false;
            }
            return buf_app(b, "]", 1);
        }
        /* record */
        if (!buf_app(b, "{", 1)) return false;
        s_value *f = s_obj_record_fields(o);
        for (s_u32 i = 0; i < o->count; ++i) {
            if (i && !buf_app(b, ", ", 2)) return false;
            if (!fmt_into(vm, f[i], b, depth + 1)) return false;
        }
        return buf_app(b, "}", 1);
    }
    }
    return false;
}

static bool push_str_from_buf(s_vm *vm, const char *buf, s_size len) {
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    if (len) memcpy(s_obj_str_data(o), buf, len);
    return push(vm, s_val_obj(o));
}

static bool builtin_print(s_vm *vm, bool newline) {
    s_value s = pop(vm);
    s_buf b;
    memset(&b, 0, sizeof(b));
    if (!fmt_into(vm, s, &b, 0)) {
        free(b.data);
        vm_trap(vm, "print: out of memory");
        return false;
    }
    if (newline) s_rt_write_line(b.data, b.len);
    else s_rt_write(b.data, b.len);
    free(b.data);
    return push(vm, s_val_none());
}

static bool builtin_str_len(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_len expects a string");
        return false;
    }
    return push(vm, s_val_int((s_i64)s_obj_str_len(s.u.o)));
}

static bool valid_utf8(const char *bytes, s_size len) {
    s_size i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i++];
        if (c <= 0x7fu) continue;
        if (c >= 0xc2u && c <= 0xdfu) {
            if (i >= len) return false;
            unsigned char d = (unsigned char)bytes[i++];
            if (d < 0x80u || d > 0xbfu) return false;
            continue;
        }
        if (c >= 0xe0u && c <= 0xefu) {
            if (i + 1u >= len) return false;
            unsigned char d = (unsigned char)bytes[i++];
            unsigned char e = (unsigned char)bytes[i++];
            if (d < 0x80u || d > 0xbfu || e < 0x80u || e > 0xbfu ||
                (c == 0xe0u && d < 0xa0u) ||
                (c == 0xedu && d > 0x9fu)) return false;
            continue;
        }
        if (c >= 0xf0u && c <= 0xf4u) {
            if (i + 2u >= len) return false;
            unsigned char d = (unsigned char)bytes[i++];
            unsigned char e = (unsigned char)bytes[i++];
            unsigned char f = (unsigned char)bytes[i++];
            if (d < 0x80u || d > 0xbfu || e < 0x80u || e > 0xbfu ||
                f < 0x80u || f > 0xbfu ||
                (c == 0xf0u && d < 0x90u) ||
                (c == 0xf4u && d > 0x8fu)) return false;
            continue;
        }
        return false;
    }
    return true;
}

static bool builtin_str_valid_utf8(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_valid_utf8 expects a string");
        return false;
    }
    return push(vm, s_val_int(valid_utf8(s_obj_str_data(s.u.o),
                                          s_obj_str_len(s.u.o)) ? 1 : 0));
}

static bool builtin_str_utf8_len(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_utf8_len expects a string");
        return false;
    }
    const char *bytes = s_obj_str_data(s.u.o);
    s_size len = s_obj_str_len(s.u.o);
    if (!valid_utf8(bytes, len)) {
        vm_trap(vm, "str_utf8_len received invalid UTF-8");
        return false;
    }
    s_size i = 0;
    s_i64 count = 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i++];
        count++;
        if (c <= 0x7fu) continue;
        if (c <= 0xdfu) i += 1u;
        else if (c <= 0xefu) i += 2u;
        else i += 3u;
    }
    return push(vm, s_val_int(count));
}

static bool builtin_array_len(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ || v.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array_len expects an array");
        return false;
    }
    return push(vm, s_val_int((s_i64)v.u.o->count));
}

static bool builtin_str_from_codepoint(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) {
        vm_trap(vm, "str_from_codepoint expects an integer");
        return false;
    }
    s_i64 cp = v.u.i;
    if (cp < 0 || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
        vm_trap(vm, "str_from_codepoint: invalid code point");
        return false;
    }
    char buf[4];
    s_size n = 0;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    return push_str_from_buf(vm, buf, n);
}

static bool builtin_str_chr_byte(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) {
        vm_trap(vm, "str_chr_byte expects an integer");
        return false;
    }
    s_i64 b = v.u.i;
    if (b < 0 || b > 255) {
        vm_trap(vm, "str_chr_byte out of range");
        return false;
    }
    char ch = (char)b;
    return push_str_from_buf(vm, &ch, 1);
}

static bool builtin_f64_to_bytes(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_FLOAT) {
        vm_trap(vm, "f64_to_bytes expects a float");
        return false;
    }
    s_u64 bits;
    memcpy(&bits, &v.u.f, 8);
    unsigned char buf[8];
    for (int i = 0; i < 8; ++i) buf[i] = (unsigned char)(bits >> (8 * i));
    return push_str_from_buf(vm, (const char *)buf, 8);
}

static bool builtin_str_byte_at(s_vm *vm) {
    s_value idx = pop(vm);
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
        idx.tag != S_VAL_INT) {
        vm_trap(vm, "str_byte_at expects (str, int)");
        return false;
    }
    s_size len = s_obj_str_len(s.u.o);
    s_i64 i = idx.u.i;
    if (i < 0 || (s_u64)i >= len) {
        vm_trap(vm, "str_byte_at out of range");
        return false;
    }
    return push(vm, s_val_int((s_u8)s_obj_str_data(s.u.o)[i]));
}

static bool builtin_bytes_to_f64(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "bytes_to_f64 expects a string");
        return false;
    }
    if (s_obj_str_len(s.u.o) < 8) {
        vm_trap(vm, "bytes_to_f64 needs 8 bytes");
        return false;
    }
    unsigned char buf[8];
    memcpy(buf, s_obj_str_data(s.u.o), 8);
    s_u64 bits = 0;
    for (int i = 0; i < 8; ++i) bits |= (s_u64)buf[i] << (8 * i);
    double d;
    memcpy(&d, &bits, 8);
    return push(vm, s_val_float(d));
}

static bool builtin_read_line(s_vm *vm) {
    s_size len;
    char *line = s_rt_read_line(&len);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    if (len) memcpy(s_obj_str_data(o), line, len);
    free(line);
    return push(vm, s_val_obj(o));
}

static bool builtin_to_str(s_vm *vm) {
    s_value v = pop(vm);
    char buf[64];
    const char *data;
    s_size len;
    switch (v.tag) {
    case S_VAL_INT:
        len = s_rt_format_i64(buf, sizeof buf, v.u.i);
        data = buf;
        break;
    case S_VAL_FLOAT:
        len = s_rt_format_f64(buf, sizeof buf, v.u.f);
        if (len >= sizeof buf) len = sizeof buf - 1;
        data = buf;
        break;
    case S_VAL_BOOL:
        data = v.u.b ? "true" : "false";
        len = v.u.b ? 4 : 5;
        break;
    default:
        vm_trap(vm, "to_str expects an int, float, or bool");
        return false;
    }
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    memcpy(s_obj_str_data(o), data, len);
    return push(vm, s_val_obj(o));
}

static bool builtin_type_of(s_vm *vm) {
    s_value v = pop(vm);
    const char *s;
    s_size len;
    switch (v.tag) {
    case S_VAL_INT: s = "int"; len = 3; break;
    case S_VAL_FLOAT: s = "float"; len = 5; break;
    case S_VAL_BOOL: s = "bool"; len = 4; break;
    case S_VAL_NONE: s = "unit"; len = 4; break;
    default:
        switch (v.u.o->kind) {
        case S_OBJ_STR: s = "str"; len = 3; break;
        case S_OBJ_ARRAY: s = "array"; len = 5; break;
        case S_OBJ_RECORD: s = "record"; len = 6; break;
        case S_OBJ_EXCEPTION: s = "exception"; len = 9; break;
        default: s = "closure"; len = 7; break;
        }
        break;
    }
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    if (len) memcpy(s_obj_str_data(o), s, len);
    return push(vm, s_val_obj(o));
}

static bool builtin_str_compare(s_vm *vm) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_OBJ || a.u.o->kind != S_OBJ_STR ||
        b.tag != S_VAL_OBJ || b.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_compare expects two strings");
        return false;
    }
    s_size al = s_obj_str_len(a.u.o), bl = s_obj_str_len(b.u.o);
    s_size n = al < bl ? al : bl;
    int c = n ? memcmp(s_obj_str_data(a.u.o), s_obj_str_data(b.u.o), n) : 0;
    if (c == 0) c = (al > bl) - (al < bl);
    return push(vm, s_val_int((s_i64)c));
}

static bool builtin_itof(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) { vm_trap(vm, "i64_to_f64 expects an int"); return false; }
    return push(vm, s_val_float((double)v.u.i));
}

static bool builtin_f64_to_str(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_FLOAT) {
        vm_trap(vm, "f64_to_str expects a float");
        return false;
    }
    char buf[64];
    s_size len = s_rt_format_f64(buf, sizeof(buf), v.u.f);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    memcpy(s_obj_str_data(o), buf, len);
    return push(vm, s_val_obj(o));
}

static bool builtin_ftoi(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_FLOAT) { vm_trap(vm, "f64_to_i64 expects a float"); return false; }
    return push(vm, s_val_int((s_i64)v.u.f));
}

static bool builtin_make_array(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) { vm_trap(vm, "make_array expects an int"); return false; }
    s_i64 n = v.u.i;
    if (n < 0 || n > (1 << 20)) { vm_trap(vm, "make_array length out of range"); return false; }
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, (s_u32)n, 0);
    if (!o) return false;
    return push(vm, s_val_obj(o));
}

static bool builtin_range(s_vm *vm) {
    s_value hi = pop(vm);
    s_value lo = pop(vm);
    if (lo.tag != S_VAL_INT || hi.tag != S_VAL_INT) {
        vm_trap(vm, "range expects int arguments");
        return false;
    }
    s_i64 n = hi.u.i - lo.u.i;
    if (n < 0) n = 0;
    if (n > (1 << 20)) { vm_trap(vm, "range too large"); return false; }
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, (s_u32)n, 0);
    if (!o) return false;
    s_value *data = s_obj_array_data(o);
    for (s_i64 i = 0; i < n; ++i) data[i] = s_val_int(lo.u.i + i);
    return push(vm, s_val_obj(o));
}

static bool builtin_substr(s_vm *vm) {
    s_value len = pop(vm);
    s_value start = pop(vm);
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
        start.tag != S_VAL_INT || len.tag != S_VAL_INT) {
        vm_trap(vm, "substr expects str, int, int");
        return false;
    }
    s_i64 slen = (s_i64)s_obj_str_len(s.u.o);
    s_i64 b = start.u.i;
    if (b < 0) b = 0;
    if (b > slen) b = slen;
    s_i64 l = len.u.i;
    if (l < 0) l = 0;
    s_i64 end = b + l;
    if (end > slen) end = slen;
    s_size n = (s_size)(end - b);
    s_heap_add_root(vm->heap, &s);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)n, n);
    s_heap_remove_root(vm->heap, &s);
    if (!o) return false;
    if (n) memcpy(s_obj_str_data(o), s_obj_str_data(s.u.o) + b, n);
    return push(vm, s_val_obj(o));
}

static bool builtin_int_from_str(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "int_from_str expects a string");
        return false;
    }
    const char *p = s_obj_str_data(s.u.o);
    s_size n = s_obj_str_len(s.u.o);
    s_size i = 0;
    bool neg = false;
    if (i < n && (p[i] == '-' || p[i] == '+')) {
        neg = (p[i] == '-');
        i++;
    }
    s_u64 v = 0;
    bool any = false;
    while (i < n && p[i] >= '0' && p[i] <= '9') {
        s_u64 d = (s_u64)(p[i] - '0');
        if (v > (UINT64_MAX - d) / 10) {
            vm_trap(vm, "int_from_str overflow");
            return false;
        }
        v = v * 10 + d;
        any = true;
        i++;
    }
    if (!any) return push(vm, s_val_int(0));
    if (i != n) return push(vm, s_val_int(0)); /* trailing garbage: not an integer */
    if (neg) {
        if (v > (s_u64)INT64_MAX + 1) {
            vm_trap(vm, "int_from_str overflow");
            return false;
        }
        if (v == (s_u64)INT64_MAX + 1) return push(vm, s_val_int(INT64_MIN));
        return push(vm, s_val_int(-(s_i64)v));
    }
    if (v > (s_u64)INT64_MAX) {
        vm_trap(vm, "int_from_str overflow");
        return false;
    }
    return push(vm, s_val_int((s_i64)v));
}

static bool builtin_str_contains(s_vm *vm) {
    s_value needle = pop(vm);
    s_value hay = pop(vm);
    if (hay.tag != S_VAL_OBJ || hay.u.o->kind != S_OBJ_STR ||
        needle.tag != S_VAL_OBJ || needle.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_contains expects two strings");
        return false;
    }
    s_size hn = s_obj_str_len(hay.u.o), nn = s_obj_str_len(needle.u.o);
    bool found = false;
    if (nn == 0) {
        found = true;
    } else if (nn <= hn) {
        const char *h = s_obj_str_data(hay.u.o);
        const char *nd = s_obj_str_data(needle.u.o);
        for (s_size i = 0; i + nn <= hn; ++i) {
            if (memcmp(h + i, nd, nn) == 0) { found = true; break; }
        }
    }
    return push(vm, s_val_int(found ? 1 : 0));
}

static bool builtin_abs(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) { vm_trap(vm, "abs expects an int"); return false; }
    if (v.u.i == INT64_MIN) { vm_trap(vm, "abs overflow"); return false; }
    s_i64 r = v.u.i < 0 ? -v.u.i : v.u.i;
    return push(vm, s_val_int(r));
}

static bool builtin_minmax(s_vm *vm, bool is_max) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_INT || b.tag != S_VAL_INT) {
        vm_trap(vm, "min/max expect int arguments");
        return false;
    }
    bool pick_b = is_max ? (b.u.i > a.u.i) : (b.u.i < a.u.i);
    return push(vm, s_val_int(pick_b ? b.u.i : a.u.i));
}

static bool builtin_range_step(s_vm *vm) {
    s_value step = pop(vm);
    s_value hi = pop(vm);
    s_value lo = pop(vm);
    if (lo.tag != S_VAL_INT || hi.tag != S_VAL_INT || step.tag != S_VAL_INT) {
        vm_trap(vm, "range_step expects int arguments");
        return false;
    }
    s_i64 s = step.u.i;
    if (s == 0) { vm_trap(vm, "range_step step must be nonzero"); return false; }
    s_i64 dist = hi.u.i - lo.u.i;
    s_i64 n;
    if (s > 0) n = dist > 0 ? (dist + s - 1) / s : 0;
    else n = dist < 0 ? (dist + s + 1) / s : 0;
    if (n < 0) n = 0;
    if (n > (1 << 20)) { vm_trap(vm, "range_step too large"); return false; }
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, (s_u32)n, 0);
    if (!o) return false;
    s_value *data = s_obj_array_data(o);
    for (s_i64 i = 0; i < n; ++i) data[i] = s_val_int(lo.u.i + i * s);
    return push(vm, s_val_obj(o));
}

static bool builtin_assert(s_vm *vm) {
    s_value msg = pop(vm);
    s_value cond = pop(vm);
    if (cond.tag != S_VAL_INT || msg.tag != S_VAL_OBJ ||
        msg.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "assert expects (bool, str)");
        return false;
    }
    if (cond.u.i == 0) {
        s_size len = s_obj_str_len(msg.u.o);
        vm_trap(vm, "assertion failed: %.*s", (int)len, s_obj_str_data(msg.u.o));
        return false;
    }
    return push(vm, s_val_none());
}

static bool builtin_exit(s_vm *vm) {
    s_value code = pop(vm);
    if (code.tag != S_VAL_INT) { vm_trap(vm, "exit expects an int"); return false; }
    vm->exit_code = (int)code.u.i;
    vm->exit_halt = true;
    return push(vm, s_val_none());
}

static bool builtin_to_hex(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) { vm_trap(vm, "to_hex expects an int"); return false; }
    char buf[80];
    int n = snprintf(buf, sizeof buf, "%llx", (unsigned long long)v.u.i);
    s_size len = (n < (int)sizeof buf) ? (s_size)n : sizeof buf - 1;
    return push_str_from_buf(vm, buf, len);
}

static bool builtin_to_bin(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT) { vm_trap(vm, "to_bin expects an int"); return false; }
    unsigned long long x = (unsigned long long)v.u.i;
    char buf[80];
    s_size len = 0;
    int started = 0;
    for (int b = 63; b >= 0; --b) {
        int bit = (int)((x >> b) & 1u);
        if (bit || started) buf[len++] = (char)(bit ? '1' : '0');
        started |= bit;
    }
    if (!started) buf[len++] = '0';
    return push_str_from_buf(vm, buf, len);
}

static bool builtin_f64_to_str_prec(s_vm *vm) {
    s_value p = pop(vm);
    s_value f = pop(vm);
    if (f.tag != S_VAL_FLOAT || p.tag != S_VAL_INT) {
        vm_trap(vm, "f64_to_str_prec expects (float, int)");
        return false;
    }
    int prec = (int)p.u.i;
    if (prec < 0) prec = 0;
    if (prec > 20) prec = 20;
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%.*f", prec, f.u.f);
    s_size len = (n < (int)sizeof buf) ? (s_size)n : sizeof buf - 1;
    return push_str_from_buf(vm, buf, len);
}

static bool builtin_str_repeat(s_vm *vm) {
    s_value n = pop(vm);
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
        n.tag != S_VAL_INT) {
        vm_trap(vm, "str_repeat expects (str, int)");
        return false;
    }
    s_i64 rep = n.u.i;
    if (rep < 0) { vm_trap(vm, "str_repeat count must be non-negative"); return false; }
    s_size slen = s_obj_str_len(s.u.o);
    s_size total;
    if (!s_size_mul_ovf(slen, (s_size)rep, &total) || total > (1u << 24)) {
        vm_trap(vm, "str_repeat too large");
        return false;
    }
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
    if (!o) return false;
    char *d = s_obj_str_data(o);
    for (s_i64 i = 0; i < rep; ++i)
        memcpy(d + (s_size)i * slen, s_obj_str_data(s.u.o), slen);
    return push(vm, s_val_obj(o));
}

static bool builtin_str_trim(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_trim expects a string");
        return false;
    }
    const char *data = s_obj_str_data(s.u.o);
    s_size len = s_obj_str_len(s.u.o);
    s_size b = 0, e = len;
    while (b < e && (data[b] == ' ' || data[b] == '\t' ||
                     data[b] == '\n' || data[b] == '\r')) b++;
    while (e > b && (data[e - 1] == ' ' || data[e - 1] == '\t' ||
                     data[e - 1] == '\n' || data[e - 1] == '\r')) e--;
    s_size n = e - b;
    /* The popped source `s` lives only in this C local; keep it rooted while
     * we allocate, or the collector may free and reuse its chunk. */
    s_heap_add_root(vm->heap, &s);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)n, n);
    s_heap_remove_root(vm->heap, &s);
    if (!o) return false;
    if (n) memcpy(s_obj_str_data(o), data + b, n);
    return push(vm, s_val_obj(o));
}

static bool builtin_args_len(s_vm *vm) {
    return push(vm, s_val_int((s_i64)vm->argc));
}

static bool builtin_args(s_vm *vm) {
    s_value i = pop(vm);
    if (i.tag != S_VAL_INT) { vm_trap(vm, "args expects an int"); return false; }
    s_i64 k = i.u.i;
    if (k < 0 || k >= (s_i64)vm->argc) {
        vm_trap(vm, "args index out of range");
        return false;
    }
    const char *s = vm->argv[k];
    s_size len = strlen(s);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    if (len) memcpy(s_obj_str_data(o), s, len);
    return push(vm, s_val_obj(o));
}

static bool builtin_str_split(s_vm *vm) {
    s_value sep = pop(vm);
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
        sep.tag != S_VAL_OBJ || sep.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_split expects (str, str)");
        return false;
    }
    const char *data = s_obj_str_data(s.u.o);
    s_size sl = s_obj_str_len(s.u.o);
    const char *sp = s_obj_str_data(sep.u.o);
    s_size pl = s_obj_str_len(sep.u.o);
    if (pl == 0) { vm_trap(vm, "str_split separator must be non-empty"); return false; }
    s_size n = 1;
    for (s_size i = 0; i + pl <= sl; ) {
        if (memcmp(data + i, sp, pl) == 0) { n++; i += pl; }
        else i++;
    }
    s_gcobj *arr = vm_alloc(vm, S_OBJ_ARRAY, (s_u32)n, 0);
    if (!arr) return false;
    s_value *dst = s_obj_array_data(arr);
    s_size start = 0, k = 0;
    for (s_size i = 0; i + pl <= sl; ) {
        if (memcmp(data + i, sp, pl) == 0) {
            s_size ln = i - start;
            s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)ln, ln);
            if (!o) return false;
            if (ln) memcpy(s_obj_str_data(o), data + start, ln);
            dst[k++] = s_val_obj(o);
            i += pl;
            start = i;
        } else {
            i++;
        }
    }
    {
        s_size ln = sl - start;
        s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)ln, ln);
        if (!o) return false;
        if (ln) memcpy(s_obj_str_data(o), data + start, ln);
        dst[k++] = s_val_obj(o);
    }
    return push(vm, s_val_obj(arr));
}

static bool builtin_str_join(s_vm *vm) {
    s_value sep = pop(vm);
    s_value arr = pop(vm);
    if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY ||
        sep.tag != S_VAL_OBJ || sep.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_join expects ([str], str)");
        return false;
    }
    s_size n = arr.u.o->count;
    const char *sp = s_obj_str_data(sep.u.o);
    s_size pl = s_obj_str_len(sep.u.o);
    s_size total = 0;
    for (s_size i = 0; i < n; ++i) {
        s_value e = s_obj_array_data(arr.u.o)[i];
        if (e.tag != S_VAL_OBJ || e.u.o->kind != S_OBJ_STR) {
            vm_trap(vm, "str_join array must contain strings");
            return false;
        }
        total += s_obj_str_len(e.u.o);
        if (i + 1 < n) total += pl;
    }
    if (total > (1u << 24)) { vm_trap(vm, "str_join too large"); return false; }
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
    if (!o) return false;
    char *d = s_obj_str_data(o);
    s_size off = 0;
    for (s_size i = 0; i < n; ++i) {
        s_value e = s_obj_array_data(arr.u.o)[i];
        s_size ln = s_obj_str_len(e.u.o);
        if (ln) { memcpy(d + off, s_obj_str_data(e.u.o), ln); off += ln; }
        if (i + 1 < n) { memcpy(d + off, sp, pl); off += pl; }
    }
    return push(vm, s_val_obj(o));
}

static bool builtin_array_slice(s_vm *vm) {
    s_value len = pop(vm);
    s_value start = pop(vm);
    s_value arr = pop(vm);
    if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY ||
        start.tag != S_VAL_INT || len.tag != S_VAL_INT) {
        vm_trap(vm, "slice expects ([T], int, int)");
        return false;
    }
    s_i64 n = (s_i64)arr.u.o->count;
    s_i64 b = start.u.i;
    if (b < 0) b = 0;
    if (b > n) b = n;
    s_i64 l = len.u.i;
    if (l < 0) l = 0;
    s_i64 end = b + l;
    if (end > n) end = n;
    s_size m = (s_size)(end - b);
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, (s_u32)m, 0);
    if (!o) return false;
    if (m) memcpy(s_obj_array_data(o), s_obj_array_data(arr.u.o) + b,
                  sizeof(s_value) * m);
    return push(vm, s_val_obj(o));
}

static bool builtin_str_map(s_vm *vm, int mode) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "string transform expects a string");
        return false;
    }
    s_size n = s_obj_str_len(s.u.o);
    const char *src = s_obj_str_data(s.u.o);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)n, n);
    if (!o) return false;
    char *dst = s_obj_str_data(o);
    if (mode == 0) {
        for (s_size i = 0; i < n; ++i) dst[i] = src[n - 1 - i];
    } else {
        for (s_size i = 0; i < n; ++i) {
            char ch = src[i];
            if (mode == 1 && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            else if (mode == 2 && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            dst[i] = ch;
        }
    }
    return push(vm, s_val_obj(o));
}

static bool builtin_len(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ) {
        vm_trap(vm, "len expects an array or string");
        return false;
    }
    if (v.u.o->kind == S_OBJ_ARRAY || v.u.o->kind == S_OBJ_STR)
        return push(vm, s_val_int((s_i64)v.u.o->count));
    vm_trap(vm, "len expects an array or string");
    return false;
}

static bool builtin_array_reverse(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ || v.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array_reverse expects an array");
        return false;
    }
    s_u32 n = v.u.o->count;
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, n, 0);
    if (!o) return false;
    s_value *src = s_obj_array_data(v.u.o);
    s_value *dst = s_obj_array_data(o);
    for (s_u32 i = 0; i < n; ++i) dst[i] = src[n - 1 - i];
    return push(vm, s_val_obj(o));
}

static bool builtin_array_reduce(s_vm *vm, int mode) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ || v.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array reduction expects an array");
        return false;
    }
    s_u32 n = v.u.o->count;
    s_value *el = s_obj_array_data(v.u.o);
    if (n == 0) {
        vm_trap(vm, "array reduction on empty array");
        return false;
    }
    for (s_u32 i = 0; i < n; ++i) {
        if (el[i].tag != S_VAL_INT) {
            vm_trap(vm, "array reduction expects an int array");
            return false;
        }
    }
    s_i64 acc = el[0].u.i;
    for (s_u32 i = 1; i < n; ++i) {
        s_i64 x = el[i].u.i;
        if (mode == 0) acc += x;
        else if (mode == 1 && x < acc) acc = x;
        else if (mode == 2 && x > acc) acc = x;
    }
    return push(vm, s_val_int(acc));
}

static bool builtin_str_to_f64(s_vm *vm) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_to_f64 expects a string");
        return false;
    }
    s_size n = s_obj_str_len(s.u.o);
    char buf[128];
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, s_obj_str_data(s.u.o), n);
    buf[n] = '\0';
    double v = strtod(buf, NULL);
    return push(vm, s_val_float(v));
}

static char *cstr_dup(const s_value *sv) {
    s_size n = s_obj_str_len(sv->u.o);
    char *buf = malloc(n + 1);
    if (!buf) return NULL;
    memcpy(buf, s_obj_str_data(sv->u.o), n);
    buf[n] = '\0';
    return buf;
}

static bool builtin_read_file(s_vm *vm) {
    s_value p = pop(vm);
    if (p.tag != S_VAL_OBJ || p.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "read_file expects a path string");
        return false;
    }
    char *path = cstr_dup(&p);
    if (!path) { vm_trap(vm, "read_file: out of memory"); return false; }
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) { vm_trap(vm, "read_file: cannot open file"); return false; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); vm_trap(vm, "read_file: seek failed"); return false; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); vm_trap(vm, "read_file: tell failed"); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); vm_trap(vm, "read_file: seek failed"); return false; }
    s_size n = (s_size)sz;
    if (n > (1u << 26)) { fclose(f); vm_trap(vm, "read_file too large"); return false; }
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)n, n);
    if (!o) { fclose(f); return false; }
    size_t got = n ? fread(s_obj_str_data(o), 1, n, f) : 0;
    fclose(f);
    if (got != n) { vm_trap(vm, "read_file: short read"); return false; }
    return push(vm, s_val_obj(o));
}

static bool builtin_write_file(s_vm *vm) {
    s_value data = pop(vm);
    s_value p = pop(vm);
    if (p.tag != S_VAL_OBJ || p.u.o->kind != S_OBJ_STR ||
        data.tag != S_VAL_OBJ || data.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "write_file expects (str, str)");
        return false;
    }
    char *path = cstr_dup(&p);
    if (!path) { vm_trap(vm, "write_file: out of memory"); return false; }
    s_size n = s_obj_str_len(data.u.o);
    const char *bytes = s_obj_str_data(data.u.o);
    FILE *f = fopen(path, "wb");
    free(path);
    if (!f) { vm_trap(vm, "write_file: cannot open file"); return false; }
    size_t w = n ? fwrite(bytes, 1, n, f) : 0;
    if (fclose(f) != 0 || w != n) {
        vm_trap(vm, "write_file: write failed");
        return false;
    }
    return push(vm, s_val_none());
}

static bool builtin_file_exists(s_vm *vm) {
    s_value p = pop(vm);
    if (p.tag != S_VAL_OBJ || p.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "file_exists expects a path string");
        return false;
    }
    char *path = cstr_dup(&p);
    if (!path) { vm_trap(vm, "file_exists: out of memory"); return false; }
    FILE *f = fopen(path, "rb");
    free(path);
    bool exists = f != NULL;
    if (f) fclose(f);
    return push(vm, s_val_int(exists ? 1 : 0));
}

static bool builtin_int_to_str_base(s_vm *vm) {
    s_value base = pop(vm);
    s_value v = pop(vm);
    if (v.tag != S_VAL_INT || base.tag != S_VAL_INT) {
        vm_trap(vm, "int_to_str_base expects (int, int)");
        return false;
    }
    s_i64 pb = base.u.i;
    if (pb < 2 || pb > 36) {
        vm_trap(vm, "int_to_str_base base must be in 2..36");
        return false;
    }
    bool neg = v.u.i < 0;
    unsigned long long x = (unsigned long long)v.u.i;
    if (neg) x = 0 - x;
    char buf[80];
    s_size len = 0;
    do {
        unsigned d = (unsigned)(x % (unsigned long long)pb);
        buf[len++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        x /= (unsigned long long)pb;
    } while (x != 0);
    s_size start = 0;
    if (neg) buf[len++] = '-';
    for (s_size i = start, j = len - 1; i < j; ++i, --j) {
        char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
    }
    return push_str_from_buf(vm, buf + start, len - start);
}

static bool builtin_str_pad(s_vm *vm) {
    s_value fill = pop(vm);
    s_value width = pop(vm);
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
        width.tag != S_VAL_INT ||
        fill.tag != S_VAL_OBJ || fill.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "str_pad expects (str, int, str)");
        return false;
    }
    s_size slen = s_obj_str_len(s.u.o);
    s_i64 w = width.u.i;
    if (w <= (s_i64)slen) return push(vm, s);
    s_size pad = (s_size)(w - (s_i64)slen);
    if (pad > (1u << 26)) { vm_trap(vm, "str_pad too wide"); return false; }
    char fc = ' ';
    if (s_obj_str_len(fill.u.o) > 0) fc = s_obj_str_data(fill.u.o)[0];
    s_size total = slen + pad;
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
    if (!o) return false;
    memset(s_obj_str_data(o), fc, pad);
    memcpy(s_obj_str_data(o) + pad, s_obj_str_data(s.u.o), slen);
    return push(vm, s_val_obj(o));
}

static bool builtin_array_concat(s_vm *vm) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_OBJ || a.u.o->kind != S_OBJ_ARRAY ||
        b.tag != S_VAL_OBJ || b.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array_concat expects two arrays");
        return false;
    }
    s_u32 na = a.u.o->count, nb = b.u.o->count;
    s_u32 n = na + nb;
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, n, 0);
    if (!o) return false;
    s_value *dst = s_obj_array_data(o);
    memcpy(dst, s_obj_array_data(a.u.o), sizeof(s_value) * na);
    memcpy(dst + na, s_obj_array_data(b.u.o), sizeof(s_value) * nb);
    return push(vm, s_val_obj(o));
}

static bool builtin_array_lookup(s_vm *vm, bool want_index) {
    s_value needle = pop(vm);
    s_value arr = pop(vm);
    if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY ||
        needle.tag != S_VAL_INT) {
        vm_trap(vm, want_index ? "array_index_of expects (array, int)"
                               : "array_contains expects (array, int)");
        return false;
    }
    s_u32 n = arr.u.o->count;
    s_value *el = s_obj_array_data(arr.u.o);
    for (s_u32 i = 0; i < n; ++i) {
        if (el[i].tag != S_VAL_INT) {
            vm_trap(vm, "array lookup expects an int array");
            return false;
        }
        if (el[i].u.i == needle.u.i) {
            return push(vm, want_index ? s_val_int((s_i64)i)
                                       : s_val_int(1));
        }
    }
    return push(vm, want_index ? s_val_int((s_i64)-1) : s_val_int(0));
}

static int cmp_i64(const void *pa, const void *pb) {
    s_i64 a = ((const s_value *)pa)->u.i;
    s_i64 b = ((const s_value *)pb)->u.i;
    return (a > b) - (a < b);
}

static bool builtin_array_sort(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ || v.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array_sort expects an array");
        return false;
    }
    s_u32 n = v.u.o->count;
    s_value *el = s_obj_array_data(v.u.o);
    for (s_u32 i = 0; i < n; ++i) {
        if (el[i].tag != S_VAL_INT) {
            vm_trap(vm, "array_sort expects an int array");
            return false;
        }
    }
    s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, n, 0);
    if (!o) return false;
    if (n) {
        memcpy(s_obj_array_data(o), el, sizeof(s_value) * n);
        qsort(s_obj_array_data(o), n, sizeof(s_value), cmp_i64);
    }
    return push(vm, s_val_obj(o));
}

static bool dispatch_builtin(s_vm *vm, s_u32 id) {
    switch (id) {
    case S_BUILTIN_PRINT: return builtin_print(vm, false);
    case S_BUILTIN_PRINTLN: return builtin_print(vm, true);
    case S_BUILTIN_STR_LEN: return builtin_str_len(vm);
    case S_BUILTIN_ARRAY_LEN: return builtin_array_len(vm);
    case S_BUILTIN_READ_LINE: return builtin_read_line(vm);
    case S_BUILTIN_TO_STR: return builtin_to_str(vm);
    case S_BUILTIN_F64_TO_STR: return builtin_f64_to_str(vm);
    case S_BUILTIN_I64_TO_F64: return builtin_itof(vm);
    case S_BUILTIN_F64_TO_I64: return builtin_ftoi(vm);
    case S_BUILTIN_MAKE_ARRAY: return builtin_make_array(vm);
    case S_BUILTIN_RANGE: return builtin_range(vm);
    case S_BUILTIN_SUBSTR: return builtin_substr(vm);
    case S_BUILTIN_STR_CONTAINS: return builtin_str_contains(vm);
    case S_BUILTIN_INT_FROM_STR: return builtin_int_from_str(vm);
    case S_BUILTIN_ABS: return builtin_abs(vm);
    case S_BUILTIN_MIN: return builtin_minmax(vm, false);
    case S_BUILTIN_MAX: return builtin_minmax(vm, true);
    case S_BUILTIN_STR_TO_F64: return builtin_str_to_f64(vm);
    case S_BUILTIN_RANGE_STEP: return builtin_range_step(vm);
    case S_BUILTIN_ASSERT: return builtin_assert(vm);
    case S_BUILTIN_EXIT: return builtin_exit(vm);
    case S_BUILTIN_TO_HEX: return builtin_to_hex(vm);
    case S_BUILTIN_TO_BIN: return builtin_to_bin(vm);
    case S_BUILTIN_F64_TO_STR_PREC: return builtin_f64_to_str_prec(vm);
    case S_BUILTIN_STR_REPEAT: return builtin_str_repeat(vm);
    case S_BUILTIN_STR_TRIM: return builtin_str_trim(vm);
    case S_BUILTIN_ARGS_LEN: return builtin_args_len(vm);
    case S_BUILTIN_ARGS: return builtin_args(vm);
    case S_BUILTIN_STR_SPLIT: return builtin_str_split(vm);
    case S_BUILTIN_STR_JOIN: return builtin_str_join(vm);
    case S_BUILTIN_ARRAY_SLICE: return builtin_array_slice(vm);
    case S_BUILTIN_STR_REVERSE: return builtin_str_map(vm, 0);
    case S_BUILTIN_STR_UPPER: return builtin_str_map(vm, 1);
    case S_BUILTIN_STR_LOWER: return builtin_str_map(vm, 2);
    case S_BUILTIN_STR_COMPARE: return builtin_str_compare(vm);
    case S_BUILTIN_TYPE_OF: return builtin_type_of(vm);
    case S_BUILTIN_LEN: return builtin_len(vm);
    case S_BUILTIN_ARRAY_REVERSE: return builtin_array_reverse(vm);
    case S_BUILTIN_ARRAY_SUM: return builtin_array_reduce(vm, 0);
    case S_BUILTIN_ARRAY_MIN: return builtin_array_reduce(vm, 1);
    case S_BUILTIN_ARRAY_MAX: return builtin_array_reduce(vm, 2);
    case S_BUILTIN_READ_FILE: return builtin_read_file(vm);
    case S_BUILTIN_WRITE_FILE: return builtin_write_file(vm);
    case S_BUILTIN_FILE_EXISTS: return builtin_file_exists(vm);
    case S_BUILTIN_INT_TO_STR_BASE: return builtin_int_to_str_base(vm);
    case S_BUILTIN_STR_PAD: return builtin_str_pad(vm);
    case S_BUILTIN_ARRAY_CONCAT: return builtin_array_concat(vm);
    case S_BUILTIN_ARRAY_CONTAINS: return builtin_array_lookup(vm, false);
    case S_BUILTIN_ARRAY_INDEX_OF: return builtin_array_lookup(vm, true);
    case S_BUILTIN_ARRAY_SORT: return builtin_array_sort(vm);
    case S_BUILTIN_STR_VALID_UTF8: return builtin_str_valid_utf8(vm);
    case S_BUILTIN_STR_UTF8_LEN: return builtin_str_utf8_len(vm);
    case S_BUILTIN_STR_FROM_CODEPOINT: return builtin_str_from_codepoint(vm);
    case S_BUILTIN_STR_CHR_BYTE: return builtin_str_chr_byte(vm);
    case S_BUILTIN_F64_TO_BYTES: return builtin_f64_to_bytes(vm);
    case S_BUILTIN_STR_BYTE_AT: return builtin_str_byte_at(vm);
    case S_BUILTIN_BYTES_TO_F64: return builtin_bytes_to_f64(vm);
    default:
        vm_trap(vm, "unknown builtin %u", id);
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Arithmetic helpers                                                  */
/* ------------------------------------------------------------------ */

static bool arith_i(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_INT || b.tag != S_VAL_INT) {
        vm_trap(vm, "integer operation on non-int");
        return false;
    }
    s_i64 r;
    bool ok = true;
    switch (op) {
    case BC_ADD_I: ok = s_i64_add_ovf(a.u.i, b.u.i, &r); break;
    case BC_SUB_I: ok = s_i64_sub_ovf(a.u.i, b.u.i, &r); break;
    case BC_MUL_I: ok = s_i64_mul_ovf(a.u.i, b.u.i, &r); break;
    case BC_DIV_I: ok = s_i64_div_ovf(a.u.i, b.u.i, &r); break;
    case BC_MOD_I:
        if (b.u.i == 0) { vm_trap(vm, "integer division by zero"); return false; }
        r = a.u.i % b.u.i;
        break;
    default: return false;
    }
    if (!ok) { vm_trap(vm, "integer arithmetic overflow"); return false; }
    return push(vm, s_val_int(r));
}

static bool arith_f(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_FLOAT || b.tag != S_VAL_FLOAT) {
        vm_trap(vm, "float operation on non-float");
        return false;
    }
    double r;
    switch (op) {
    case BC_ADD_F: r = a.u.f + b.u.f; break;
    case BC_SUB_F: r = a.u.f - b.u.f; break;
    case BC_MUL_F: r = a.u.f * b.u.f; break;
    case BC_DIV_F:
        if (b.u.f == 0.0) { vm_trap(vm, "float division by zero"); return false; }
        r = a.u.f / b.u.f;
        break;
    default: return false;
    }
    return push(vm, s_val_float(r));
}

static bool cmp_i(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_INT || b.tag != S_VAL_INT) return false;
    bool r;
    switch (op) {
    case BC_EQ_I: r = a.u.i == b.u.i; break;
    case BC_NE_I: r = a.u.i != b.u.i; break;
    case BC_LT_I: r = a.u.i < b.u.i; break;
    case BC_LE_I: r = a.u.i <= b.u.i; break;
    case BC_GT_I: r = a.u.i > b.u.i; break;
    case BC_GE_I: r = a.u.i >= b.u.i; break;
    default: return false;
    }
    return push(vm, s_val_int(r ? 1 : 0));
}

static bool cmp_f(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_FLOAT || b.tag != S_VAL_FLOAT) return false;
    bool r;
    switch (op) {
    case BC_EQ_F: r = a.u.f == b.u.f; break;
    case BC_NE_F: r = a.u.f != b.u.f; break;
    case BC_LT_F: r = a.u.f < b.u.f; break;
    case BC_LE_F: r = a.u.f <= b.u.f; break;
    case BC_GT_F: r = a.u.f > b.u.f; break;
    case BC_GE_F: r = a.u.f >= b.u.f; break;
    default: return false;
    }
    return push(vm, s_val_int(r ? 1 : 0));
}

static bool cmp_s(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    if (a.tag != S_VAL_OBJ || b.tag != S_VAL_OBJ) return false;
    s_size la = s_obj_str_len(a.u.o), lb = s_obj_str_len(b.u.o);
    int m = memcmp(s_obj_str_data(a.u.o), s_obj_str_data(b.u.o), la < lb ? la : lb);
    bool r;
    switch (op) {
    case BC_EQ_S: r = (la == lb) && (m == 0); break;
    case BC_NE_S: r = !((la == lb) && (m == 0)); break;
    case BC_LT_S: r = (m < 0) || (m == 0 && la < lb); break;
    case BC_LE_S: r = (m < 0) || (m == 0 && la <= lb); break;
    case BC_GT_S: r = (m > 0) || (m == 0 && la > lb); break;
    case BC_GE_S: r = (m > 0) || (m == 0 && la >= lb); break;
    default: return false;
    }
    return push(vm, s_val_int(r ? 1 : 0));
}

static bool cmp_b(s_vm *vm, int op) {
    s_value b = pop(vm);
    s_value a = pop(vm);
    bool r;
    if (op == BC_EQ_B) r = (a.tag == b.tag) && (a.u.i == b.u.i);
    else r = !((a.tag == b.tag) && (a.u.i == b.u.i));
    return push(vm, s_val_int(r ? 1 : 0));
}

/* ------------------------------------------------------------------ */
/* Execution                                                           */
/* ------------------------------------------------------------------ */

int s_vm_run(s_vm *vm, s_heap *heap) {
    if (!vm || !vm->m) return 1;
    if (s_verify_module(vm->m) != 0) {
        vm_trap(vm, "refusing to execute unverified bytecode");
        return 1;
    }
    vm->heap = heap;
    s_heap_set_incremental(heap, vm->gc_incremental);
    vm->sp = 0;
    vm->fp = 0;
    vm->ncalls = 0;
    vm->nhandlers = 0;
    vm->trapped = false;
    vm->exit_code = 0;

    const s_bc_module *m = vm->m;
    s_u32 fid = m->entry;
    vm->cur_fid = fid;
    const s_bc_func *f = &m->funcs[fid];
    if (!grow_stack(vm, f->frame_size + f->max_operand_depth + 4)) {
        vm_trap(vm, "stack too large");
        return 1;
    }
    memset(vm->st, 0, sizeof(s_value) * f->frame_size);
    vm->fp = 0;
    vm->sp = f->frame_size;
    s_u32 pc = 0;

    bool done = false;
    while (!done && !vm->trapped) {
        const s_bc_ins *in = &f->ins[pc];
        if (getenv("SURU_TRACE"))
            printf("fid=%u pc=%u op=%-10s sp=%zu fp=%zu a=%u b=%u\n",
                   vm->cur_fid, pc, s_bc_op_table[in->op].name, vm->sp,
                   vm->fp, in->a, in->b);
        switch (in->op) {
        case BC_NOP:
            pc++; break;
        case BC_CONST_INT:
            if (!push(vm, s_val_int(m->ints[in->a]))) return 1;
            pc++; break;
        case BC_CONST_FLOAT:
            if (!push(vm, s_val_float(m->floats[in->a]))) return 1;
            pc++; break;
        case BC_CONST_STR: {
            s_size len = m->strlens[in->a];
            s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
            if (!o) return 1;
            if (len) memcpy(s_obj_str_data(o), m->strings[in->a], len);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_CONST_NONE:
            if (!push(vm, s_val_none())) return 1;
            pc++; break;
        case BC_LD_LOCAL:
            if (!push(vm, vm->st[vm->fp + in->a])) return 1;
            pc++; break;
        case BC_ST_LOCAL:
            vm->st[vm->fp + in->a] = pop(vm);
            pc++; break;
        case BC_POP:
            (void)pop(vm);
            pc++; break;
        case BC_DUP: {
            if (!push(vm, vm->st[vm->sp - 1])) return 1;
            pc++; break;
        }
        case BC_SWAP: {
            s_value a = vm->st[vm->sp - 1];
            vm->st[vm->sp - 1] = vm->st[vm->sp - 2];
            vm->st[vm->sp - 2] = a;
            pc++; break;
        }
        case BC_NOT: {
            s_value v = pop(vm);
            if (v.tag != S_VAL_INT) { vm_trap(vm, "not expects a bool"); return 1; }
            if (!push(vm, s_val_int(v.u.i ? 0 : 1))) return 1;
            pc++; break;
        }
        case BC_NEG_I: {
            s_value v = pop(vm);
            s_i64 r;
            if (v.tag != S_VAL_INT || !s_i64_neg_ovf(v.u.i, &r)) {
                vm_trap(vm, "integer negation overflow/type error");
                return 1;
            }
            if (!push(vm, s_val_int(r))) return 1;
            pc++; break;
        }
        case BC_NEG_F: {
            s_value v = pop(vm);
            if (v.tag != S_VAL_FLOAT) { vm_trap(vm, "float negation type error"); return 1; }
            if (!push(vm, s_val_float(-v.u.f))) return 1;
            pc++; break;
        }
        case BC_CONV_I_F: {
            s_value v = pop(vm);
            if (v.tag != S_VAL_INT) { vm_trap(vm, "conv_i_f type error"); return 1; }
            if (!push(vm, s_val_float((double)v.u.i))) return 1;
            pc++; break;
        }
        case BC_ADD_I: case BC_SUB_I: case BC_MUL_I:
        case BC_DIV_I: case BC_MOD_I:
            if (!arith_i(vm, in->op)) return 1;
            pc++; break;
        case BC_ADD_F: case BC_SUB_F: case BC_MUL_F: case BC_DIV_F:
            if (!arith_f(vm, in->op)) return 1;
            pc++; break;
        case BC_BIT_AND: case BC_BIT_OR: case BC_BIT_XOR:
        case BC_SHL: case BC_SHR: {
            s_value b = pop(vm);
            s_value a = pop(vm);
            if (a.tag != S_VAL_INT || b.tag != S_VAL_INT) {
                vm_trap(vm, "bitwise/shift operation on non-int");
                return 1;
            }
            s_u64 x = (s_u64)a.u.i, y = (s_u64)b.u.i;
            s_i64 r;
            switch (in->op) {
            case BC_BIT_AND: r = (s_i64)(x & y); break;
            case BC_BIT_OR: r = (s_i64)(x | y); break;
            case BC_BIT_XOR: r = (s_i64)(x ^ y); break;
            case BC_SHL: r = (y >= 64) ? 0 : (s_i64)(x << y); break;
            default: r = (y >= 64) ? 0 : (s_i64)(x >> y); break;
            }
            if (!push(vm, s_val_int(r))) return 1;
            pc++; break;
        }
        case BC_BIT_NOT: {
            s_value a = pop(vm);
            if (a.tag != S_VAL_INT) { vm_trap(vm, "bitwise not on non-int"); return 1; }
            if (!push(vm, s_val_int(~a.u.i))) return 1;
            pc++; break;
        }
        case BC_EQ_I: case BC_NE_I: case BC_LT_I: case BC_LE_I:
        case BC_GT_I: case BC_GE_I:
            if (!cmp_i(vm, in->op)) return 1;
            pc++; break;
        case BC_EQ_F: case BC_NE_F: case BC_LT_F: case BC_LE_F:
        case BC_GT_F: case BC_GE_F:
            if (!cmp_f(vm, in->op)) return 1;
            pc++; break;
        case BC_EQ_S: case BC_NE_S: case BC_LT_S: case BC_LE_S:
        case BC_GT_S: case BC_GE_S:
            if (!cmp_s(vm, in->op)) return 1;
            pc++; break;
        case BC_CONCAT_S: {
            s_value b = pop(vm);
            s_value a = pop(vm);
            if (a.tag != S_VAL_OBJ || b.tag != S_VAL_OBJ ||
                a.u.o->kind != S_OBJ_STR || b.u.o->kind != S_OBJ_STR) {
                vm_trap(vm, "concat type error");
                return 1;
            }
            s_size la = s_obj_str_len(a.u.o), lb = s_obj_str_len(b.u.o);
            s_size total;
            if (!s_size_add_ovf(la, lb, &total)) {
                vm_trap(vm, "concat overflow");
                return 1;
            }
            s_heap_add_root(vm->heap, &a);
            s_heap_add_root(vm->heap, &b);
            s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
            s_heap_remove_root(vm->heap, &b);
            s_heap_remove_root(vm->heap, &a);
            if (!o) return 1;
            char *d = s_obj_str_data(o);
            memcpy(d, s_obj_str_data(a.u.o), la);
            memcpy(d + la, s_obj_str_data(b.u.o), lb);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_REPEAT_S: {
            s_value n = pop(vm);
            s_value s = pop(vm);
            if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR ||
                n.tag != S_VAL_INT) {
                vm_trap(vm, "repeat type error");
                return 1;
            }
            s_i64 rep = n.u.i;
            if (rep < 0) { vm_trap(vm, "repeat count must be non-negative"); return 1; }
            s_size slen = s_obj_str_len(s.u.o);
            s_size total;
            if (!s_size_mul_ovf(slen, (s_size)rep, &total) || total > (1u << 24)) {
                vm_trap(vm, "repeat too large");
                return 1;
            }
            s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
            if (!o) return 1;
            char *d = s_obj_str_data(o);
            for (s_i64 i = 0; i < rep; ++i)
                memcpy(d + (s_size)i * slen, s_obj_str_data(s.u.o), slen);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_EQ_B: case BC_NE_B:
            if (!cmp_b(vm, in->op)) return 1;
            pc++; break;
        case BC_NEW_ARRAY: {
            s_u32 n = in->a;
            if (vm->sp < n) { vm_trap(vm, "array underflow"); return 1; }
            s_gcobj *o = vm_alloc(vm, S_OBJ_ARRAY, n, 0);
            if (!o) return 1;
            s_value *dst = s_obj_array_data(o);
            vm->sp -= n;
            memcpy(dst, vm->st + vm->sp, sizeof(s_value) * n);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_NEW_RECORD: {
            s_u32 n = in->b;
            if (vm->sp < n) { vm_trap(vm, "record underflow"); return 1; }
            s_gcobj *o = vm_alloc(vm, S_OBJ_RECORD, n, 0);
            if (!o) return 1;
            s_value *dst = s_obj_record_fields(o);
            vm->sp -= n;
            memcpy(dst, vm->st + vm->sp, sizeof(s_value) * n);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_NEW_EXCEPTION: {
            s_value cause = pop(vm);
            s_value message = pop(vm);
            s_value type = pop(vm);
            if (type.tag != S_VAL_OBJ || type.u.o->kind != S_OBJ_STR ||
                message.tag != S_VAL_OBJ || message.u.o->kind != S_OBJ_STR ||
                (cause.tag != S_VAL_NONE &&
                 (cause.tag != S_VAL_OBJ || cause.u.o->kind != S_OBJ_EXCEPTION))) {
                vm_trap(vm, "new_exception type error");
                return 1;
            }
            s_gcobj *o = s_heap_new_exception(vm->heap, type, message, cause);
            if (!o) { vm_trap(vm, "out of memory"); return 1; }
            if (!push(vm, s_val_obj(o))) return 1;
            pc++;
            break;
        }
        case BC_IDX_LD: {
            s_value idx = pop(vm);
            s_value arr = pop(vm);
            if (arr.tag != S_VAL_OBJ || idx.tag != S_VAL_INT ||
                (arr.u.o->kind != S_OBJ_ARRAY && arr.u.o->kind != S_OBJ_STR)) {
                vm_trap(vm, "index type error");
                return 1;
            }
            s_i64 i = idx.u.i;
            if (arr.u.o->kind == S_OBJ_STR) {
                if (i < 0) i += (s_i64)arr.u.o->count;
                if (i < 0 || i >= (s_i64)arr.u.o->count) {
                    vm_trap(vm, "string index out of bounds (%lld of %u)",
                            (long long)i, arr.u.o->count);
                    return 1;
                }
                s_gcobj *o = vm_alloc(vm, S_OBJ_STR, 1, 1);
                if (!o) return 1;
                s_obj_str_data(o)[0] = s_obj_str_data(arr.u.o)[i];
                if (!push(vm, s_val_obj(o))) return 1;
                pc++; break;
            }
            if (i < 0) i += (s_i64)arr.u.o->count;
            if (i < 0 || i >= (s_i64)arr.u.o->count) {
                vm_trap(vm, "index out of bounds (%lld of %u)", (long long)i, arr.u.o->count);
                return 1;
            }
            if (!push(vm, s_obj_array_data(arr.u.o)[i])) return 1;
            pc++; break;
        }
        case BC_IDX_ST: {
            s_value val = pop(vm);
            s_value idx = pop(vm);
            s_value arr = pop(vm);
            if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY ||
                idx.tag != S_VAL_INT) {
                vm_trap(vm, "index store type error");
                return 1;
            }
            s_i64 i = idx.u.i;
            if (i < 0) i += (s_i64)arr.u.o->count;
            if (i < 0 || i >= (s_i64)arr.u.o->count) {
                vm_trap(vm, "index out of bounds (%lld of %u)", (long long)i, arr.u.o->count);
                return 1;
            }
            s_heap_write_barrier(vm->heap, arr.u.o, val);
            s_obj_array_data(arr.u.o)[i] = val;
            pc++; break;
        }
        case BC_ARR_LEN: {
            s_value arr = pop(vm);
            if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY) {
                vm_trap(vm, "array_len type error");
                return 1;
            }
            if (!push(vm, s_val_int((s_i64)arr.u.o->count))) return 1;
            pc++; break;
        }
        case BC_FIELD_LD: {
            s_value rec = pop(vm);
            if (rec.tag != S_VAL_OBJ || rec.u.o->kind != S_OBJ_RECORD) {
                vm_trap(vm, "field load type error");
                return 1;
            }
            if (in->b >= rec.u.o->count) { vm_trap(vm, "field out of bounds"); return 1; }
            if (!push(vm, s_obj_record_fields(rec.u.o)[in->b])) return 1;
            pc++; break;
        }
        case BC_FIELD_ST: {
            s_value val = pop(vm);
            s_value rec = pop(vm);
            if (rec.tag != S_VAL_OBJ || rec.u.o->kind != S_OBJ_RECORD) {
                vm_trap(vm, "field store type error");
                return 1;
            }
            if (in->b >= rec.u.o->count) { vm_trap(vm, "field out of bounds"); return 1; }
            s_heap_write_barrier(vm->heap, rec.u.o, val);
            s_obj_record_fields(rec.u.o)[in->b] = val;
            pc++; break;
        }
        case BC_CALL: {
            s_u32 callee = in->a;
            const s_bc_func *cf = &m->funcs[callee];
            s_u32 nargs = cf->nparams;
            if (vm->sp < nargs) { vm_trap(vm, "call underflow"); return 1; }
            s_size fp_new = vm->sp - nargs;
            s_size need = fp_new + cf->frame_size + cf->max_operand_depth + 4;
            if (!grow_stack(vm, need)) {
                vm_trap(vm, "stack overflow in call");
                return 1;
            }
            for (s_size i = fp_new + nargs; i < fp_new + cf->frame_size; ++i)
                vm->st[i] = s_val_int(0);
            if (!grow_calls(vm)) { vm_trap(vm, "call stack overflow"); return 1; }
            vm->calls[vm->ncalls++] =
                (s_cont){ pc + 1, vm->fp, vm->cur_fid, fp_new };
            vm->cur_fid = callee;
            vm->fp = fp_new;
            vm->sp = fp_new + cf->frame_size;
            f = cf;
            pc = 0;
            break;
        }
        case BC_PUSH_HANDLER:
            if (!grow_handlers(vm)) {
                vm_trap(vm, "handler stack overflow");
                return 1;
            }
            vm->handlers[vm->nhandlers++] = (s_handler){
                vm->cur_fid, in->a, in->b, vm->fp, vm->sp, vm->ncalls
            };
            pc++;
            break;
        case BC_POP_HANDLER:
            if (vm->nhandlers == 0 ||
                vm->handlers[vm->nhandlers - 1].fid != vm->cur_fid ||
                vm->handlers[vm->nhandlers - 1].call_depth != vm->ncalls) {
                vm_trap(vm, "handler stack underflow");
                return 1;
            }
            vm->nhandlers--;
            pc++;
            break;
        case BC_THROW: {
            s_value exception = pop(vm);
            if (!vm_throw(vm, exception, m, &f, &pc)) return 1;
            break;
        }
        case BC_MKCLOSURE: {
            s_gcobj *o = vm_alloc(vm, S_OBJ_CLOSURE, 1, 0);
            if (!o) return 1;
            s_obj_array_data(o)[0] = s_val_int((s_i64)in->a);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_MKCLOSURE_V: {
            s_u32 ncap = in->b;
            if (vm->sp < ncap) { vm_trap(vm, "mkclosure_v underflow"); return 1; }
            s_gcobj *env = vm_alloc(vm, S_OBJ_ARRAY, ncap, 0);
            if (!env) return 1;
            vm->sp -= ncap;
            memcpy(s_obj_array_data(env), vm->st + vm->sp,
                   sizeof(s_value) * ncap);
            s_gcobj *o = vm_alloc(vm, S_OBJ_CLOSURE, 2, 0);
            if (!o) return 1;
            s_obj_array_data(o)[0] = s_val_int((s_i64)in->a);
            s_obj_array_data(o)[1] = s_val_obj(env);
            if (!push(vm, s_val_obj(o))) return 1;
            pc++; break;
        }
        case BC_CALLV: {
            s_u32 nargs = in->a;
            if (vm->sp < nargs + 1) {
                vm_trap(vm, "call_v underflow");
                return 1;
            }
            s_size closure_slot = vm->sp - nargs - 1;
            s_value cl = vm->st[closure_slot];
            if (cl.tag != S_VAL_OBJ || cl.u.o->kind != S_OBJ_CLOSURE) {
                vm_trap(vm, "call_v expects a closure");
                return 1;
            }
            s_value fnv = s_obj_array_data(cl.u.o)[0];
            if (fnv.tag != S_VAL_INT || fnv.u.i < 0 ||
                (s_u64)fnv.u.i >= m->nfuncs) {
                vm_trap(vm, "call_v invalid function reference");
                return 1;
            }
            s_u32 callee = (s_u32)fnv.u.i;
            const s_bc_func *cf = &m->funcs[callee];
            bool has_env = (cl.u.o->count == 2);
            s_u32 want = has_env ? nargs + 1 : nargs;
            if (cf->nparams != want) {
                vm_trap(vm, "call_v arity mismatch");
                return 1;
            }
            s_size fp_new;
            if (has_env) {
                vm->st[closure_slot] = s_obj_array_data(cl.u.o)[1];
                fp_new = closure_slot;
            } else {
                fp_new = closure_slot + 1;
            }
            s_size need = fp_new + cf->frame_size + cf->max_operand_depth + 4;
            if (!grow_stack(vm, need)) {
                vm_trap(vm, "stack overflow in call");
                return 1;
            }
            for (s_size i = fp_new + want; i < fp_new + cf->frame_size; ++i)
                vm->st[i] = s_val_int(0);
            if (!grow_calls(vm)) {
                vm_trap(vm, "call stack overflow");
                return 1;
            }
            vm->calls[vm->ncalls++] =
                (s_cont){ pc + 1, vm->fp, vm->cur_fid, fp_new };
            vm->cur_fid = callee;
            vm->fp = fp_new;
            vm->sp = fp_new + cf->frame_size;
            f = cf;
            pc = 0;
            break;
        }
        case BC_RET:
        case BC_RET_UNIT: {
            s_value result = s_val_none();
            if (in->op == BC_RET) {
                result = pop(vm);
            }
            discard_frame_handlers(vm);
            if (vm->ncalls == 0) {
                if (result.tag == S_VAL_INT) vm->exit_code = (int)result.u.i;
                else vm->exit_code = 0;
                done = true;
                break;
            }
            s_cont c = vm->calls[--vm->ncalls];
            vm->cur_fid = c.fid;
            f = &m->funcs[c.fid];
            vm->fp = c.fp;
            vm->sp = c.ret_slot;
            if (!push(vm, result)) return 1;
            pc = c.pc;
            break;
        }
        case BC_JMP:
            pc = in->a;
            break;
        case BC_JZ: {
            s_value v = pop(vm);
            if (v.tag != S_VAL_INT) { vm_trap(vm, "branch condition type error"); return 1; }
            pc = v.u.i == 0 ? in->a : pc + 1;
            break;
        }
        case BC_JNZ: {
            s_value v = pop(vm);
            if (v.tag != S_VAL_INT) { vm_trap(vm, "branch condition type error"); return 1; }
            pc = v.u.i != 0 ? in->a : pc + 1;
            break;
        }
        case BC_BUILTIN:
            if (!dispatch_builtin(vm, in->a)) return 1;
            if (vm->exit_halt) {
                done = true;
                break;
            }
            pc++; break;
        case BC_TRAP:
            vm_trap(vm, "executed trap instruction");
            return 1;
        default:
            vm_trap(vm, "invalid opcode %u", in->op);
            return 1;
        }
    }

    return vm->exit_code;
}
