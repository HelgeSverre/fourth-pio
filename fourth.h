// fourth.h — A tiny Forth where PIO instructions are first-class data
// "Nobody has built it" → hold my beer
#ifndef FOURTH_H
#define FOURTH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── Value types ─────────────────────────────────────────────────
// Every cell on the stack is a tagged value so we can have
// integers, PIO instruction maps, and lists side by side.

#define VAL_INT     0
#define VAL_INSTR   1  // PIO instruction (structured map)
#define VAL_LIST    2  // array of values
#define VAL_BOOL    3
#define VAL_BYTES   4  // assembled bytecode
#define VAL_STR     5  // string (for error messages etc)

// PIO instruction — the structured "map" representation
// Every field is explicit, not bit-packed
typedef struct {
    uint8_t  op;         // 0=jmp,1=wait,2=in,3=out,4=push/pull,5=mov,6=irq,7=set
    uint8_t  cond;       // jmp condition
    uint8_t  target;     // jmp target address (resolved label)
    uint8_t  polarity;   // wait
    uint8_t  source;     // wait/in/mov source
    uint8_t  dest;       // out/mov/set dest
    uint8_t  bits;       // in/out bit count
    uint8_t  index;      // wait/irq index
    uint8_t  mode;       // irq mode: 0=set,1=wait,2=clear
    uint8_t  value;      // set value
    uint8_t  mov_op;     // mov operation: 0=none,1=invert,2=reverse
    int8_t   delay;      // delay cycles (-1 = unset)
    int8_t   side_set;   // side-set value (-1 = unset)
    bool     block;      // push/pull blocking
    bool     iffull;     // push iffull
    bool     ifempty;    // pull ifempty
    bool     rel;        // wait/irq relative
    bool     is_pull;    // distinguish push vs pull (both op=4)
    char     label[16];  // label name for jmp target (pre-resolution)
} PioInstr;

// A list of values
#define MAX_LIST 64
typedef struct {
    int count;
    // Forward declare — actual values stored as indices into a pool
    int items[MAX_LIST];  // indices into the value pool
} ValueList;

// Assembled program output
typedef struct {
    uint16_t words[32];
    int      length;
    uint8_t  wrap_target;
    uint8_t  wrap;
} PioProgram;

// Tagged value
typedef struct {
    uint8_t type;
    union {
        int32_t    integer;
        bool       boolean;
        PioInstr   instr;
        ValueList  list;
        PioProgram program;
        char       str[64];
    } as;
} Value;

// ── Forth VM ────────────────────────────────────────────────────

#define STACK_SIZE 256
#define DICT_SIZE  128
#define MAX_VALUES 512
#define INPUT_SIZE 1024

typedef struct ForthVM ForthVM;

// Word function pointer
typedef void (*WordFn)(ForthVM *vm);

typedef struct {
    char  name[32];
    WordFn fn;
} DictEntry;

struct ForthVM {
    Value     stack[STACK_SIZE];
    int       sp;                  // stack pointer (top of stack)

    DictEntry dict[DICT_SIZE];
    int       dict_count;

    // Label table for assembler
    struct {
        char    name[16];
        uint8_t addr;
    } labels[32];
    int label_count;

    // Input parsing
    char  input[INPUT_SIZE];
    int   input_pos;

    // Error state
    bool  error;
    char  error_msg[128];

    // Output buffer for .s and friends
    char  output[2048];
    int   output_pos;
};

// ── API ─────────────────────────────────────────────────────────

void   fourth_init(ForthVM *vm);
void   fourth_eval(ForthVM *vm, const char *line);
void   fourth_push(ForthVM *vm, Value v);
Value  fourth_pop(ForthVM *vm);
Value  fourth_peek(ForthVM *vm, int depth);  // 0 = top
int    fourth_depth(ForthVM *vm);

// Value constructors
Value  val_int(int32_t n);
Value  val_bool(bool b);
Value  val_instr(PioInstr instr);
Value  val_list(void);
Value  val_str(const char *s);

// PIO instruction constructors (return default-initialized PioInstr)
PioInstr pio_jmp(const char *label, uint8_t cond);
PioInstr pio_wait(uint8_t polarity, uint8_t source, uint8_t index);
PioInstr pio_in(uint8_t source, uint8_t bits);
PioInstr pio_out(uint8_t dest, uint8_t bits);
PioInstr pio_push_instr(bool iffull, bool block);
PioInstr pio_pull_instr(bool ifempty, bool block);
PioInstr pio_mov(uint8_t dest, uint8_t source, uint8_t op);
PioInstr pio_irq(uint8_t mode, uint8_t index, bool rel);
PioInstr pio_set(uint8_t dest, uint8_t value);
PioInstr pio_nop(void);

#endif // FOURTH_H
