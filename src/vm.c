#include "suru/vm.h"
#include "suru/verify.h"
#include "suru/stdlib.h"
#include "suru/runtime.h"

#include <string.h>
#include <stdarg.h>

#define VM_INIT_STACK 4096u
#define VM_MAX_STACK (1u << 22)
#define VM_GC_INIT_THRESHOLD (1u << 20)

typedef struct s_cont {
    s_u32 pc;
    s_size fp;
    s_u32 fid;
} s_cont;

struct s_vm {
    const s_bc_module *m;
    s_heap *heap;
    s_value *st;
    s_size st_cap;
    s_size sp;
    s_size fp;
    s_u32 cur_fid;
    s_cont *calls;
    s_size ncalls, capcalls;
    s_u32 alloc_counter;
    s_u64 gc_threshold;
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
    return vm;
}

void s_vm_destroy(s_vm *vm) {
    if (!vm) return;
    free(vm->st);
    free(vm->calls);
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

static void maybe_collect(s_vm *vm) {
    vm->alloc_counter++;
    bool should = vm->alloc_counter >= 512 ||
                  s_heap_live_bytes(vm->heap) > vm->gc_threshold;
    if (should) {
        s_heap_set_stack_root(vm->heap, vm->st, vm->sp);
        s_heap_collect(vm->heap);
        if (vm->gc_threshold < (s_u64)1 << 40)
            vm->gc_threshold *= 2;
        vm->alloc_counter = 0;
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

static bool builtin_print(s_vm *vm, bool newline) {
    s_value s = pop(vm);
    if (s.tag != S_VAL_OBJ || s.u.o->kind != S_OBJ_STR) {
        vm_trap(vm, "print expects a string");
        return false;
    }
    s_size len = s_obj_str_len(s.u.o);
    const char *data = s_obj_str_data(s.u.o);
    if (newline) s_rt_write_line(data, len);
    else s_rt_write(data, len);
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

static bool builtin_array_len(s_vm *vm) {
    s_value v = pop(vm);
    if (v.tag != S_VAL_OBJ || v.u.o->kind != S_OBJ_ARRAY) {
        vm_trap(vm, "array_len expects an array");
        return false;
    }
    return push(vm, s_val_int((s_i64)v.u.o->count));
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
    if (v.tag != S_VAL_INT) {
        vm_trap(vm, "to_str expects an int");
        return false;
    }
    char buf[40];
    s_size len = s_rt_format_i64(buf, sizeof(buf), v.u.i);
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)len, len);
    if (!o) return false;
    memcpy(s_obj_str_data(o), buf, len);
    return push(vm, s_val_obj(o));
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
    s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)n, n);
    if (!o) return false;
    if (n) memcpy(s_obj_str_data(o), s_obj_str_data(s.u.o) + b, n);
    return push(vm, s_val_obj(o));
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
    vm->sp = 0;
    vm->fp = 0;
    vm->ncalls = 0;
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
            printf("fid=%u pc=%u op=%-10s sp=%zu\n",
                   vm->cur_fid, pc, s_bc_op_table[in->op].name, vm->sp);
        switch (in->op) {
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
        case BC_ADD_I: case BC_SUB_I: case BC_MUL_I:
        case BC_DIV_I: case BC_MOD_I:
            if (!arith_i(vm, in->op)) return 1;
            pc++; break;
        case BC_ADD_F: case BC_SUB_F: case BC_MUL_F: case BC_DIV_F:
            if (!arith_f(vm, in->op)) return 1;
            pc++; break;
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
            s_gcobj *o = vm_alloc(vm, S_OBJ_STR, (s_u32)total, total);
            if (!o) return 1;
            char *d = s_obj_str_data(o);
            memcpy(d, s_obj_str_data(a.u.o), la);
            memcpy(d + la, s_obj_str_data(b.u.o), lb);
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
        case BC_IDX_LD: {
            s_value idx = pop(vm);
            s_value arr = pop(vm);
            if (arr.tag != S_VAL_OBJ || arr.u.o->kind != S_OBJ_ARRAY ||
                idx.tag != S_VAL_INT) {
                vm_trap(vm, "index type error");
                return 1;
            }
            s_i64 i = idx.u.i;
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
            if (i < 0 || i >= (s_i64)arr.u.o->count) {
                vm_trap(vm, "index out of bounds (%lld of %u)", (long long)i, arr.u.o->count);
                return 1;
            }
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
                (s_cont){ pc + 1, vm->fp, vm->cur_fid };
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
            vm->sp = vm->fp;
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
