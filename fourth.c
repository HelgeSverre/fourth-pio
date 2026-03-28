// fourth.c — A tiny Forth where PIO instructions are first-class data
// Proving that Forth can do structural PIO manipulation.

#include "fourth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

// ── Helpers ─────────────────────────────────────────────────────

static void vm_error(ForthVM *vm, const char *msg) {
    vm->error = true;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "%s", msg);
}

static void vm_output(ForthVM *vm, const char *fmt, ...) {
    int remaining = (int)sizeof(vm->output) - vm->output_pos;
    if (remaining <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(vm->output + vm->output_pos, remaining, fmt, ap);
    va_end(ap);
    if (written > 0) {
        vm->output_pos += (written < remaining) ? written : remaining - 1;
    }
}

// ── Value constructors ──────────────────────────────────────────

Value val_int(int32_t n) {
    Value v = {0};
    v.type = VAL_INT;
    v.as.integer = n;
    return v;
}

Value val_bool(bool b) {
    Value v = {0};
    v.type = VAL_BOOL;
    v.as.boolean = b;
    return v;
}

Value val_instr(PioInstr instr) {
    Value v = {0};
    v.type = VAL_INSTR;
    v.as.instr = instr;
    return v;
}

Value val_list(void) {
    Value v = {0};
    v.type = VAL_LIST;
    v.as.list.count = 0;
    return v;
}

Value val_str(const char *s) {
    Value v = {0};
    v.type = VAL_STR;
    snprintf(v.as.str, sizeof(v.as.str), "%s", s);
    return v;
}

// ── Stack operations ────────────────────────────────────────────

void fourth_push(ForthVM *vm, Value v) {
    if (vm->sp >= STACK_SIZE) {
        vm_error(vm, "stack overflow");
        return;
    }
    vm->stack[vm->sp++] = v;
}

Value fourth_pop(ForthVM *vm) {
    if (vm->sp <= 0) {
        vm_error(vm, "stack underflow");
        Value v = {0};
        return v;
    }
    return vm->stack[--vm->sp];
}

Value fourth_peek(ForthVM *vm, int depth) {
    int idx = vm->sp - 1 - depth;
    if (idx < 0) {
        vm_error(vm, "stack underflow on peek");
        Value v = {0};
        return v;
    }
    return vm->stack[idx];
}

int fourth_depth(ForthVM *vm) {
    return vm->sp;
}

// ── PIO instruction constructors ────────────────────────────────

static PioInstr pio_base(uint8_t op) {
    PioInstr i = {0};
    i.op = op;
    i.delay = -1;
    i.side_set = -1;
    i.block = true;
    return i;
}

PioInstr pio_nop(void) {
    PioInstr i = pio_base(5); // mov
    i.dest = 2;    // y
    i.source = 2;  // y
    return i;
}

PioInstr pio_jmp(const char *label, uint8_t cond) {
    PioInstr i = pio_base(0);
    i.cond = cond;
    snprintf(i.label, sizeof(i.label), "%s", label);
    return i;
}

PioInstr pio_wait(uint8_t polarity, uint8_t source, uint8_t index) {
    PioInstr i = pio_base(1);
    i.polarity = polarity;
    i.source = source;
    i.index = index;
    return i;
}

PioInstr pio_in(uint8_t source, uint8_t bits) {
    PioInstr i = pio_base(2);
    i.source = source;
    i.bits = bits;
    return i;
}

PioInstr pio_out(uint8_t dest, uint8_t bits) {
    PioInstr i = pio_base(3);
    i.dest = dest;
    i.bits = bits;
    return i;
}

PioInstr pio_push_instr(bool iffull, bool block) {
    PioInstr i = pio_base(4);
    i.is_pull = false;
    i.iffull = iffull;
    i.block = block;
    return i;
}

PioInstr pio_pull_instr(bool ifempty, bool block) {
    PioInstr i = pio_base(4);
    i.is_pull = true;
    i.ifempty = ifempty;
    i.block = block;
    return i;
}

PioInstr pio_mov(uint8_t dest, uint8_t source, uint8_t op) {
    PioInstr i = pio_base(5);
    i.dest = dest;
    i.source = source;
    i.mov_op = op;
    return i;
}

PioInstr pio_irq(uint8_t mode, uint8_t index, bool rel) {
    PioInstr i = pio_base(6);
    i.mode = mode;
    i.index = index;
    i.rel = rel;
    return i;
}

PioInstr pio_set(uint8_t dest, uint8_t value) {
    PioInstr i = pio_base(7);
    i.dest = dest;
    i.value = value;
    return i;
}

// ── Name lookup tables ──────────────────────────────────────────

static const char *OP_NAMES[] = {
    "jmp", "wait", "in", "out", "push/pull", "mov", "irq", "set"
};

static const char *JMP_COND_NAMES[] = {
    "", "!x", "x--", "!y", "y--", "x!=y", "pin", "!osre"
};

static const char *IN_SRC_NAMES[] = {
    "pins", "x", "y", "null", "?", "?", "isr", "osr"
};

static const char *OUT_DEST_NAMES[] = {
    "pins", "x", "y", "null", "pindirs", "pc", "isr", "exec"
};

static const char *MOV_DEST_NAMES[] = {
    "pins", "x", "y", "?", "exec", "pc", "isr", "osr"
};

static const char *MOV_SRC_NAMES[] = {
    "pins", "x", "y", "null", "?", "status", "isr", "osr"
};

static const char *SET_DEST_NAMES[] = {
    "pins", "x", "y", "?", "pindirs"
};

static const char *WAIT_SRC_NAMES[] = {
    "gpio", "pin", "irq"
};

static const char *IRQ_MODE_NAMES[] = {
    "set", "wait", "clear"
};

// ── Keyword → code lookups ──────────────────────────────────────

static int lookup_name(const char *name, const char *table[], int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(name, table[i]) == 0) return i;
    }
    return -1;
}

// ── Print a value ───────────────────────────────────────────────

static void print_instr(ForthVM *vm, PioInstr *i) {
    switch (i->op) {
    case 0: // jmp
        vm_output(vm, "{jmp %s%s%s",
            JMP_COND_NAMES[i->cond],
            i->cond ? " " : "",
            i->label[0] ? i->label : "?");
        break;
    case 1: // wait
        vm_output(vm, "{wait %d %s %d%s",
            i->polarity, WAIT_SRC_NAMES[i->source], i->index,
            i->rel ? " rel" : "");
        break;
    case 2: // in
        vm_output(vm, "{in %s %d", IN_SRC_NAMES[i->source],
            i->bits == 0 ? 32 : i->bits);
        break;
    case 3: // out
        vm_output(vm, "{out %s %d", OUT_DEST_NAMES[i->dest],
            i->bits == 0 ? 32 : i->bits);
        break;
    case 4: // push/pull
        if (i->is_pull)
            vm_output(vm, "{pull%s%s",
                i->ifempty ? " ifempty" : "",
                i->block ? "" : " noblock");
        else
            vm_output(vm, "{push%s%s",
                i->iffull ? " iffull" : "",
                i->block ? "" : " noblock");
        break;
    case 5: // mov
        vm_output(vm, "{mov %s %s%s%s",
            MOV_DEST_NAMES[i->dest],
            i->mov_op == 1 ? "!" : "",
            MOV_SRC_NAMES[i->source],
            i->mov_op == 2 ? " reverse" : "");
        break;
    case 6: // irq
        vm_output(vm, "{irq %s %d%s",
            IRQ_MODE_NAMES[i->mode], i->index,
            i->rel ? " rel" : "");
        break;
    case 7: // set
        vm_output(vm, "{set %s %d",
            i->dest < 5 ? SET_DEST_NAMES[i->dest] : "?", i->value);
        break;
    }
    if (i->delay >= 0) vm_output(vm, " [%d]", i->delay);
    if (i->side_set >= 0) vm_output(vm, " side %d", i->side_set);
    vm_output(vm, "}");
}

static void print_value(ForthVM *vm, Value *v) {
    switch (v->type) {
    case VAL_INT:
        vm_output(vm, "%d", v->as.integer);
        break;
    case VAL_BOOL:
        vm_output(vm, "%s", v->as.boolean ? "true" : "false");
        break;
    case VAL_INSTR:
        print_instr(vm, &v->as.instr);
        break;
    case VAL_LIST:
        vm_output(vm, "[ ");
        for (int i = 0; i < v->as.list.count; i++) {
            // Lists store copies inline in the stack
            // We need a way to print them... store Values directly
        }
        vm_output(vm, "](%d)", v->as.list.count);
        break;
    case VAL_BYTES:
        vm_output(vm, "<program %d words>", v->as.program.length);
        break;
    case VAL_STR:
        vm_output(vm, "\"%s\"", v->as.str);
        break;
    }
}

// ── Word parsing ────────────────────────────────────────────────

static void skip_whitespace(ForthVM *vm) {
    while (vm->input[vm->input_pos] && isspace(vm->input[vm->input_pos]))
        vm->input_pos++;
}

static bool next_word(ForthVM *vm, char *buf, int bufsize) {
    skip_whitespace(vm);
    if (!vm->input[vm->input_pos]) return false;

    int i = 0;
    while (vm->input[vm->input_pos] && !isspace(vm->input[vm->input_pos]) && i < bufsize - 1) {
        buf[i++] = vm->input[vm->input_pos++];
    }
    buf[i] = '\0';
    return true;
}

// ── Built-in words ──────────────────────────────────────────────

// Stack manipulation
static void w_dup(ForthVM *vm) {
    if (vm->sp < 1) { vm_error(vm, "dup: stack underflow"); return; }
    fourth_push(vm, fourth_peek(vm, 0));
}

static void w_drop(ForthVM *vm) { fourth_pop(vm); }

static void w_swap(ForthVM *vm) {
    if (vm->sp < 2) { vm_error(vm, "swap: need 2"); return; }
    Value a = vm->stack[vm->sp - 1];
    vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2];
    vm->stack[vm->sp - 2] = a;
}

static void w_over(ForthVM *vm) {
    if (vm->sp < 2) { vm_error(vm, "over: need 2"); return; }
    fourth_push(vm, fourth_peek(vm, 1));
}

static void w_rot(ForthVM *vm) {
    if (vm->sp < 3) { vm_error(vm, "rot: need 3"); return; }
    Value a = vm->stack[vm->sp - 3];
    vm->stack[vm->sp - 3] = vm->stack[vm->sp - 2];
    vm->stack[vm->sp - 2] = vm->stack[vm->sp - 1];
    vm->stack[vm->sp - 1] = a;
}

// Arithmetic
static void w_add(ForthVM *vm) {
    Value b = fourth_pop(vm), a = fourth_pop(vm);
    if (vm->error) return;
    fourth_push(vm, val_int(a.as.integer + b.as.integer));
}

static void w_sub(ForthVM *vm) {
    Value b = fourth_pop(vm), a = fourth_pop(vm);
    if (vm->error) return;
    fourth_push(vm, val_int(a.as.integer - b.as.integer));
}

static void w_mul(ForthVM *vm) {
    Value b = fourth_pop(vm), a = fourth_pop(vm);
    if (vm->error) return;
    fourth_push(vm, val_int(a.as.integer * b.as.integer));
}

// Print
static void w_dot(ForthVM *vm) {
    Value v = fourth_pop(vm);
    if (vm->error) return;
    print_value(vm, &v);
    vm_output(vm, " ");
}

static void w_dot_s(ForthVM *vm) {
    vm_output(vm, "<%d> ", vm->sp);
    for (int i = 0; i < vm->sp; i++) {
        print_value(vm, &vm->stack[i]);
        vm_output(vm, " ");
    }
}

static void w_cr(ForthVM *vm) {
    vm_output(vm, "\n");
}

// ── PIO instruction words ───────────────────────────────────────
// These push structured PioInstr values onto the stack

static void w_pio_nop(ForthVM *vm) {
    fourth_push(vm, val_instr(pio_nop()));
}

static void w_pio_jmp(ForthVM *vm) {
    // ( label-str cond -- instr )
    // Simple form: ( label-str -- instr )  with cond=always
    char word[32];
    // Parse the label from the next input word
    if (!next_word(vm, word, sizeof(word))) {
        vm_error(vm, "pio-jmp: expected label"); return;
    }
    char cond_word[32] = "";
    // Check if there's a condition keyword
    int saved_pos = vm->input_pos;
    if (next_word(vm, cond_word, sizeof(cond_word))) {
        // Check if it's a valid condition
        int c = -1;
        if (strcmp(cond_word, "always") == 0) c = 0;
        else if (strcmp(cond_word, "!x") == 0) c = 1;
        else if (strcmp(cond_word, "x--") == 0) c = 2;
        else if (strcmp(cond_word, "!y") == 0) c = 3;
        else if (strcmp(cond_word, "y--") == 0) c = 4;
        else if (strcmp(cond_word, "x!=y") == 0) c = 5;
        else if (strcmp(cond_word, "pin") == 0) c = 6;
        else if (strcmp(cond_word, "!osre") == 0) c = 7;

        if (c >= 0) {
            fourth_push(vm, val_instr(pio_jmp(word, c)));
            return;
        }
        // Not a condition — put it back
        vm->input_pos = saved_pos;
    }
    fourth_push(vm, val_instr(pio_jmp(word, 0))); // always
}

static void w_pio_wait(ForthVM *vm) {
    // Parse: polarity source index [rel]
    char w1[32], w2[32], w3[32];
    if (!next_word(vm, w1, sizeof(w1)) ||
        !next_word(vm, w2, sizeof(w2)) ||
        !next_word(vm, w3, sizeof(w3))) {
        vm_error(vm, "pio-wait: need polarity source index"); return;
    }
    int pol = atoi(w1);
    int src = lookup_name(w2, WAIT_SRC_NAMES, 3);
    if (src < 0) { vm_error(vm, "pio-wait: bad source (gpio/pin/irq)"); return; }
    int idx = atoi(w3);
    PioInstr i = pio_wait(pol, src, idx);

    // Check for optional 'rel'
    int saved = vm->input_pos;
    char w4[32];
    if (next_word(vm, w4, sizeof(w4)) && strcmp(w4, "rel") == 0) {
        i.rel = true;
    } else {
        vm->input_pos = saved;
    }
    fourth_push(vm, val_instr(i));
}

static void w_pio_in(ForthVM *vm) {
    char w1[32], w2[32];
    if (!next_word(vm, w1, sizeof(w1)) || !next_word(vm, w2, sizeof(w2))) {
        vm_error(vm, "pio-in: need source bits"); return;
    }
    int src = lookup_name(w1, IN_SRC_NAMES, 8);
    if (src < 0) { vm_error(vm, "pio-in: bad source"); return; }
    int bits = atoi(w2);
    if (bits == 32) bits = 0;
    fourth_push(vm, val_instr(pio_in(src, bits)));
}

static void w_pio_out(ForthVM *vm) {
    char w1[32], w2[32];
    if (!next_word(vm, w1, sizeof(w1)) || !next_word(vm, w2, sizeof(w2))) {
        vm_error(vm, "pio-out: need dest bits"); return;
    }
    int dest = lookup_name(w1, OUT_DEST_NAMES, 8);
    if (dest < 0) { vm_error(vm, "pio-out: bad dest"); return; }
    int bits = atoi(w2);
    if (bits == 32) bits = 0;
    fourth_push(vm, val_instr(pio_out(dest, bits)));
}

static void w_pio_push(ForthVM *vm) {
    fourth_push(vm, val_instr(pio_push_instr(false, true)));
}

static void w_pio_pull(ForthVM *vm) {
    fourth_push(vm, val_instr(pio_pull_instr(false, true)));
}

static void w_pio_mov(ForthVM *vm) {
    char w1[32], w2[32];
    if (!next_word(vm, w1, sizeof(w1)) || !next_word(vm, w2, sizeof(w2))) {
        vm_error(vm, "pio-mov: need dest source"); return;
    }

    // Handle !source prefix for invert
    uint8_t mov_op = 0;
    const char *src_name = w2;
    if (w2[0] == '!') {
        mov_op = 1;
        src_name = w2 + 1;
    }

    int dest = lookup_name(w1, MOV_DEST_NAMES, 8);
    if (dest < 0) { vm_error(vm, "pio-mov: bad dest"); return; }
    int src = lookup_name(src_name, MOV_SRC_NAMES, 8);
    if (src < 0) { vm_error(vm, "pio-mov: bad source"); return; }

    // Check for optional operation keyword
    int saved = vm->input_pos;
    char w3[32];
    if (next_word(vm, w3, sizeof(w3))) {
        if (strcmp(w3, "invert") == 0) mov_op = 1;
        else if (strcmp(w3, "reverse") == 0) mov_op = 2;
        else vm->input_pos = saved;
    }

    fourth_push(vm, val_instr(pio_mov(dest, src, mov_op)));
}

static void w_pio_irq(ForthVM *vm) {
    char w1[32], w2[32];
    if (!next_word(vm, w1, sizeof(w1)) || !next_word(vm, w2, sizeof(w2))) {
        vm_error(vm, "pio-irq: need mode index"); return;
    }
    int mode = lookup_name(w1, IRQ_MODE_NAMES, 3);
    if (mode < 0) { vm_error(vm, "pio-irq: bad mode (set/wait/clear)"); return; }
    int idx = atoi(w2);
    bool rel = false;
    int saved = vm->input_pos;
    char w3[32];
    if (next_word(vm, w3, sizeof(w3)) && strcmp(w3, "rel") == 0) {
        rel = true;
    } else {
        vm->input_pos = saved;
    }
    fourth_push(vm, val_instr(pio_irq(mode, idx, rel)));
}

static void w_pio_set(ForthVM *vm) {
    char w1[32], w2[32];
    if (!next_word(vm, w1, sizeof(w1)) || !next_word(vm, w2, sizeof(w2))) {
        vm_error(vm, "pio-set: need dest value"); return;
    }
    int dest = lookup_name(w1, SET_DEST_NAMES, 5);
    if (dest < 0) { vm_error(vm, "pio-set: bad dest (pins/x/y/pindirs)"); return; }
    int val = atoi(w2);
    fourth_push(vm, val_instr(pio_set(dest, val)));
}

// ── Instruction modifiers (composable!) ─────────────────────────
// These take an instruction from the stack, modify a field, push it back

static void w_pio_delay(ForthVM *vm) {
    // ( instr delay -- instr' )
    Value delay_v = fourth_pop(vm);
    Value instr_v = fourth_pop(vm);
    if (vm->error) return;
    if (instr_v.type != VAL_INSTR) { vm_error(vm, "delay: not an instruction"); return; }
    instr_v.as.instr.delay = delay_v.as.integer;
    fourth_push(vm, instr_v);
}

static void w_pio_side(ForthVM *vm) {
    // ( instr side-val -- instr' )
    Value side_v = fourth_pop(vm);
    Value instr_v = fourth_pop(vm);
    if (vm->error) return;
    if (instr_v.type != VAL_INSTR) { vm_error(vm, "side: not an instruction"); return; }
    instr_v.as.instr.side_set = side_v.as.integer;
    fourth_push(vm, instr_v);
}

// ── List operations ─────────────────────────────────────────────
// Lists store indices into a secondary storage area on the VM
// But for simplicity, we'll use a different approach:
// [ starts collecting, ] ends and pushes the list

// We use a separate "list building" stack
static bool building_list = false;
static int list_build_mark = 0;  // stack depth when [ was called

static void w_list_begin(ForthVM *vm) {
    // Mark current stack depth — everything pushed after this becomes the list
    list_build_mark = vm->sp;
    building_list = true;
}

#define LIST_STORE_SIZE 2048
static Value list_store[LIST_STORE_SIZE];
static int list_store_next = 0;

static Value *list_item(Value *list, int idx) {
    if (list->type != VAL_LIST || idx < 0 || idx >= list->as.list.count) return NULL;
    return &list_store[list->as.list.items[idx]];
}

// Override list_end to use proper store
static void w_list_end_v2(ForthVM *vm) {
    if (!building_list) { vm_error(vm, "]: no matching ["); return; }
    building_list = false;

    int count = vm->sp - list_build_mark;
    if (count > MAX_LIST) { vm_error(vm, "]: list too long (max 64)"); return; }

    Value list = val_list();
    list.as.list.count = count;

    for (int i = 0; i < count; i++) {
        int slot = list_store_next++;
        if (slot >= LIST_STORE_SIZE) {
            vm_error(vm, "]: list store exhausted");
            return;
        }
        list_store[slot] = vm->stack[list_build_mark + i];
        list.as.list.items[i] = slot;
    }

    vm->sp = list_build_mark;
    fourth_push(vm, list);
}

// List manipulation words
static void w_list_len(ForthVM *vm) {
    Value v = fourth_pop(vm);
    if (vm->error) return;
    if (v.type != VAL_LIST) { vm_error(vm, "len: not a list"); return; }
    fourth_push(vm, val_int(v.as.list.count));
}

static void w_list_nth(ForthVM *vm) {
    // ( list n -- item )
    Value n = fourth_pop(vm);
    Value list = fourth_pop(vm);
    if (vm->error) return;
    if (list.type != VAL_LIST) { vm_error(vm, "nth: not a list"); return; }
    Value *item = list_item(&list, n.as.integer);
    if (!item) { vm_error(vm, "nth: out of bounds"); return; }
    fourth_push(vm, *item);
}

static void w_list_append(ForthVM *vm) {
    // ( list item -- list' )
    Value item = fourth_pop(vm);
    Value list = fourth_pop(vm);
    if (vm->error) return;
    if (list.type != VAL_LIST) { vm_error(vm, "append: not a list"); return; }
    if (list.as.list.count >= MAX_LIST) { vm_error(vm, "append: list full"); return; }

    int slot = list_store_next++;
    if (slot >= LIST_STORE_SIZE) { vm_error(vm, "append: store full"); return; }
    list_store[slot] = item;
    list.as.list.items[list.as.list.count] = slot;
    list.as.list.count++;
    fourth_push(vm, list);
}

// Print list with actual contents
static void w_list_print(ForthVM *vm) {
    Value v = fourth_pop(vm);
    if (vm->error) return;
    if (v.type != VAL_LIST) { vm_error(vm, ".list: not a list"); return; }
    vm_output(vm, "[ ");
    for (int i = 0; i < v.as.list.count; i++) {
        Value *item = list_item(&v, i);
        if (item) print_value(vm, item);
        vm_output(vm, " ");
    }
    vm_output(vm, "] (%d items)", v.as.list.count);
}

// ── MAP: transform each instruction in a list ───────────────────
// This is the key structural manipulation capability!

// map-delay: ( list n -- list' )  — add delay to every instruction
static void w_map_delay(ForthVM *vm) {
    Value n = fourth_pop(vm);
    Value list = fourth_pop(vm);
    if (vm->error) return;
    if (list.type != VAL_LIST) { vm_error(vm, "map-delay: not a list"); return; }

    Value new_list = val_list();
    for (int i = 0; i < list.as.list.count; i++) {
        Value *item = list_item(&list, i);
        if (!item) continue;
        Value new_item = *item;
        if (new_item.type == VAL_INSTR) {
            new_item.as.instr.delay = n.as.integer;
        }
        if (list_store_next >= LIST_STORE_SIZE) { vm_error(vm, "map-delay: store full"); return; }
        int slot = list_store_next++;
        list_store[slot] = new_item;
        new_list.as.list.items[new_list.as.list.count++] = slot;
    }
    fourth_push(vm, new_list);
}

// filter-op: ( list op-name -- list' )  — keep only instructions with given opcode
static void w_filter_op(ForthVM *vm) {
    // Parse op name from input
    char opname[32];
    if (!next_word(vm, opname, sizeof(opname))) {
        vm_error(vm, "filter-op: need opcode name"); return;
    }
    Value list = fourth_pop(vm);
    if (vm->error) return;
    if (list.type != VAL_LIST) { vm_error(vm, "filter-op: not a list"); return; }

    int target_op = lookup_name(opname, OP_NAMES, 8);
    if (target_op < 0) { vm_error(vm, "filter-op: unknown opcode"); return; }

    Value new_list = val_list();
    for (int i = 0; i < list.as.list.count; i++) {
        Value *item = list_item(&list, i);
        if (!item || item->type != VAL_INSTR) continue;
        if (item->as.instr.op == target_op) {
            if (list_store_next >= LIST_STORE_SIZE) { vm_error(vm, "filter-op: store full"); return; }
            int slot = list_store_next++;
            list_store[slot] = *item;
            new_list.as.list.items[new_list.as.list.count++] = slot;
        }
    }
    fourth_push(vm, new_list);
}

// concat: ( list1 list2 -- list3 )
static void w_list_concat(ForthVM *vm) {
    Value b = fourth_pop(vm);
    Value a = fourth_pop(vm);
    if (vm->error) return;
    if (a.type != VAL_LIST || b.type != VAL_LIST) {
        vm_error(vm, "concat: need two lists"); return;
    }
    Value new_list = val_list();
    for (int i = 0; i < a.as.list.count; i++) {
        Value *item = list_item(&a, i);
        if (list_store_next >= LIST_STORE_SIZE) { vm_error(vm, "concat: store full"); return; }
        int slot = list_store_next++;
        list_store[slot] = *item;
        new_list.as.list.items[new_list.as.list.count++] = slot;
    }
    for (int i = 0; i < b.as.list.count; i++) {
        Value *item = list_item(&b, i);
        if (list_store_next >= LIST_STORE_SIZE) { vm_error(vm, "concat: store full"); return; }
        int slot = list_store_next++;
        list_store[slot] = *item;
        new_list.as.list.items[new_list.as.list.count++] = slot;
    }
    fourth_push(vm, new_list);
}

// ── PIO Assembler ───────────────────────────────────────────────
// Takes a list of PioInstr values and produces uint16_t machine code
// THIS is the proof that Forth can assemble structured PIO data

static uint8_t encode_bit_count(int bits) {
    return bits == 32 ? 0 : (uint8_t)(bits & 0x1F);
}

static void w_pio_assemble(ForthVM *vm) {
    Value list = fourth_pop(vm);
    if (vm->error) return;
    if (list.type != VAL_LIST) { vm_error(vm, "pio-assemble: need a list"); return; }

    // Pass 1: collect labels (look for string items as labels)
    // For simplicity, we use instruction indices
    vm->label_count = 0;

    // In our Forth, labels are added with the 'label:' word
    // For now, jmp targets are resolved by label name matching

    // Pass 2: encode
    PioProgram prog = {0};
    uint8_t side_set_bits = 0;  // TODO: configurable
    uint8_t delay_bits = 5 - side_set_bits;

    for (int i = 0; i < list.as.list.count; i++) {
        Value *item = list_item(&list, i);
        if (!item || item->type != VAL_INSTR) continue;
        if (prog.length >= 32) {
            vm_error(vm, "pio-assemble: program exceeds 32 instructions");
            return;
        }

        PioInstr *pi = &item->as.instr;
        uint16_t opcode = pi->op;
        uint8_t arg = 0;

        switch (pi->op) {
        case 0: { // jmp
            // Resolve label
            uint8_t addr = 0;
            if (pi->label[0]) {
                bool found = false;
                // Scan list for label markers
                uint8_t scan_addr = 0;
                for (int j = 0; j < list.as.list.count; j++) {
                    Value *scan = list_item(&list, j);
                    if (!scan) continue;
                    if (scan->type == VAL_STR && strcmp(scan->as.str, pi->label) == 0) {
                        addr = scan_addr;
                        found = true;
                        break;
                    }
                    if (scan->type == VAL_INSTR) scan_addr++;
                }
                if (!found) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "pio-assemble: undefined label '%s'", pi->label);
                    vm_error(vm, msg);
                    return;
                }
            }
            arg = (pi->cond << 5) | (addr & 0x1F);
            break;
        }
        case 1: // wait
            arg = (pi->polarity << 7) | (pi->source << 5) |
                  (pi->rel ? (pi->index | 0x10) : (pi->index & 0x1F));
            break;
        case 2: // in
            arg = (pi->source << 5) | encode_bit_count(pi->bits);
            break;
        case 3: // out
            arg = (pi->dest << 5) | encode_bit_count(pi->bits);
            break;
        case 4: // push/pull
            // RP2040: bit7=R/W(0=push,1=pull), bit6=IfFull/IfEmpty, bit5=Block(1=stall)
            if (pi->is_pull)
                arg = (1 << 7) | ((pi->ifempty ? 1 : 0) << 6) | ((pi->block ? 1 : 0) << 5);
            else
                arg = ((pi->iffull ? 1 : 0) << 6) | ((pi->block ? 1 : 0) << 5);
            break;
        case 5: // mov
            arg = (pi->dest << 5) | (pi->mov_op << 3) | pi->source;
            break;
        case 6: // irq
            // RP2040: bit7=0, bit6=Clr, bit5=Wait, bit4=Rel, bits3-0=Index
            arg = ((pi->mode == 2 ? 1 : 0) << 6)   // clear
                | ((pi->mode == 1 ? 1 : 0) << 5)    // wait
                | ((pi->rel ? 1 : 0) << 4)
                | (pi->index & 0x0F);
            break;
        case 7: // set
            arg = (pi->dest << 5) | (pi->value & 0x1F);
            break;
        }

        uint8_t delay = (pi->delay >= 0) ? pi->delay : 0;
        uint8_t side = (pi->side_set >= 0) ? pi->side_set : 0;
        uint8_t ds_field = (side << delay_bits) | (delay & ((1 << delay_bits) - 1));

        prog.words[prog.length] = (opcode << 13) | ((uint16_t)ds_field << 8) | arg;
        prog.length++;
    }

    prog.wrap_target = 0;
    prog.wrap = prog.length > 0 ? prog.length - 1 : 0;

    // Push the assembled program
    Value result = {0};
    result.type = VAL_BYTES;
    result.as.program = prog;
    fourth_push(vm, result);
}

// Print assembled program as hex
static void w_pio_hexdump(ForthVM *vm) {
    Value v = fourth_pop(vm);
    if (vm->error) return;
    if (v.type != VAL_BYTES) { vm_error(vm, "hexdump: not a program"); return; }

    vm_output(vm, "PIO program (%d instructions):\n", v.as.program.length);
    for (int i = 0; i < v.as.program.length; i++) {
        uint16_t w = v.as.program.words[i];
        uint8_t op = (w >> 13) & 0x7;
        vm_output(vm, "  %2d: 0x%04x  (%s)\n", i, w, OP_NAMES[op]);
    }
    vm_output(vm, "  wrap_target=%d wrap=%d\n",
        v.as.program.wrap_target, v.as.program.wrap);
}

// ── Label word ──────────────────────────────────────────────────
// Pushes a string marker that the assembler recognizes as a label

static void w_label(ForthVM *vm) {
    char name[32];
    if (!next_word(vm, name, sizeof(name))) {
        vm_error(vm, "label: need name"); return;
    }
    // Strip trailing colon if present
    int len = strlen(name);
    if (len > 0 && name[len-1] == ':') name[len-1] = '\0';
    fourth_push(vm, val_str(name));
}

// ── Comment word ────────────────────────────────────────────────
static void w_comment(ForthVM *vm) {
    // Skip until end of line
    vm->input[vm->input_pos] = '\0';
}

// ── Dictionary registration ─────────────────────────────────────

static void register_word(ForthVM *vm, const char *name, WordFn fn) {
    if (vm->dict_count >= DICT_SIZE) return;
    DictEntry *e = &vm->dict[vm->dict_count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->fn = fn;
}

void fourth_init(ForthVM *vm) {
    memset(vm, 0, sizeof(*vm));
    list_store_next = 0;

    // Stack ops
    register_word(vm, "dup",  w_dup);
    register_word(vm, "drop", w_drop);
    register_word(vm, "swap", w_swap);
    register_word(vm, "over", w_over);
    register_word(vm, "rot",  w_rot);

    // Arithmetic
    register_word(vm, "+", w_add);
    register_word(vm, "-", w_sub);
    register_word(vm, "*", w_mul);

    // Print
    register_word(vm, ".",   w_dot);
    register_word(vm, ".s",  w_dot_s);
    register_word(vm, "cr",  w_cr);

    // PIO instructions
    register_word(vm, "pio-nop",  w_pio_nop);
    register_word(vm, "pio-jmp",  w_pio_jmp);
    register_word(vm, "pio-wait", w_pio_wait);
    register_word(vm, "pio-in",   w_pio_in);
    register_word(vm, "pio-out",  w_pio_out);
    register_word(vm, "pio-push", w_pio_push);
    register_word(vm, "pio-pull", w_pio_pull);
    register_word(vm, "pio-mov",  w_pio_mov);
    register_word(vm, "pio-irq",  w_pio_irq);
    register_word(vm, "pio-set",  w_pio_set);

    // Instruction modifiers
    register_word(vm, "delay", w_pio_delay);
    register_word(vm, "side",  w_pio_side);

    // Lists
    register_word(vm, "[",      w_list_begin);
    register_word(vm, "]",      w_list_end_v2);
    register_word(vm, "len",    w_list_len);
    register_word(vm, "nth",    w_list_nth);
    register_word(vm, "append", w_list_append);
    register_word(vm, ".list",  w_list_print);
    register_word(vm, "concat", w_list_concat);

    // List transforms (the magic!)
    register_word(vm, "map-delay",  w_map_delay);
    register_word(vm, "filter-op",  w_filter_op);

    // Assembler
    register_word(vm, "pio-assemble", w_pio_assemble);
    register_word(vm, "hexdump",      w_pio_hexdump);

    // Labels
    register_word(vm, "label:", w_label);

    // Comments
    register_word(vm, "\\", w_comment);
}

// ── Interpreter ─────────────────────────────────────────────────

void fourth_eval(ForthVM *vm, const char *line) {
    snprintf(vm->input, sizeof(vm->input), "%s", line);
    vm->input_pos = 0;
    vm->error = false;
    vm->output_pos = 0;
    vm->output[0] = '\0';

    char word[64];
    while (next_word(vm, word, sizeof(word)) && !vm->error) {
        // Try dictionary
        bool found = false;
        for (int i = vm->dict_count - 1; i >= 0; i--) {
            if (strcmp(vm->dict[i].name, word) == 0) {
                vm->dict[i].fn(vm);
                found = true;
                break;
            }
        }
        if (found) continue;

        // Try parse as integer
        char *end;
        long val = strtol(word, &end, 0);
        if (*end == '\0') {
            fourth_push(vm, val_int((int32_t)val));
            continue;
        }

        // Unknown word
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown word: %s", word);
        vm_error(vm, msg);
    }
}
