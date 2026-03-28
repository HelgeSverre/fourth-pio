# Fourth PIO

An experiment in concatenative PIO programming. A tiny [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language)) interpreter (~1,150 lines of C) where RP2040 [PIO](https://en.wikipedia.org/wiki/RP2040) instructions are structured data on a stack, not packed integers.

> **Fair warning:** This is an exploration, not a tool. I have not tested it on hardware. I have no meaningful experience with PIO or embedded development. The assembler output matches `adafruit_pioasm` across 62 test cases, but that is the extent of verification. Use at your own risk.

## The idea

Concatenative languages should map naturally to PIO instruction manipulation — postfix modifiers, stack-based composition, lists as program fragments. This explores that idea. Every survey of PIO tooling mentions Forth as a natural fit, but nobody seems to have actually built one. So here it is.

```forth
[ pio-set pindirs 1
  label: loop
  pio-set pins 1 31 delay
  pio-set pins 0 31 delay
  pio-jmp loop
] pio-assemble hexdump
```

```
PIO program (4 instructions):
   0: 0xe081  (set)
   1: 0xff01  (set)
   2: 0xff00  (set)
   3: 0x0001  (jmp)
  wrap_target=0 wrap=3
```

---

## Examples

### Instructions are data you can print

```forth
pio-set pins 1 .          \ -> {set pins 1}
pio-jmp loop !x .         \ -> {jmp !x loop}
pio-mov x !y .            \ -> {mov x !y}
pio-wait 1 gpio 0 .       \ -> {wait 1 gpio 0}
```

### Modifiers compose

```forth
pio-set pins 1 3 delay .          \ -> {set pins 1 [3]}
pio-nop 7 delay 2 side .          \ -> {mov y y [7] side 2}
```

Delay and side-set are applied after construction, to any instruction, in any order.

### map-delay -- transform every instruction at once

```forth
[ pio-set pins 1  pio-nop  pio-set pins 0  pio-nop ] 15 map-delay .list
\ -> [ {set pins 1 [15]} {mov y y [15]} {set pins 0 [15]} {mov y y [15]} ]
```

### filter-op -- structural queries

```forth
[ pio-set pindirs 1  pio-set pins 1  pio-nop  pio-jmp loop ] filter-op set .list
\ -> [ {set pindirs 1} {set pins 1} ]
```

### concat -- compose fragments

```forth
[ pio-set pindirs 1 ] [ pio-set pins 1  pio-set pins 0 ] concat .list
\ -> [ {set pindirs 1} {set pins 1} {set pins 0} ]
```

### Assemble a WS2812 driver

```forth
[ label: bitloop
  pio-out x 1
  pio-jmp bitloop !x
  pio-set pins 0
  pio-pull
  pio-out null 32
] pio-assemble hexdump
```

---

## Verification

The assembler is tested against [`adafruit_pioasm`](https://github.com/adafruit/Adafruit_CircuitPython_PIOASM) as a reference oracle. `test_fourth.c` assembles 62 programs and emits the machine code as JSON. `test_oracle.py` assembles the same programs with `adafruit_pioasm` and compares word-for-word. All 62 match.

Test cases cover all 9 PIO opcodes, all 8 jump conditions, all source/destination variants, delays, and multi-instruction programs with label resolution.

---

## Prior art and inspiration

| Tool | What it does | What Fourth PIO borrowed or learned |
|------|-------------|-------------------------------------|
| MicroPython `@asm_pio` | Decorator-based PIO DSL, emits raw uint16 | The instruction set encoding tables |
| CircuitPython `adafruit_pioasm` | Text-to-integers assembler | Used as the test oracle |
| Rust `pio` crate | Typed instruction structs, compile-time assembly | The idea of a structured intermediate representation (closest existing analogue) |
| C SDK `pio_encode_*` | One-shot encode functions returning uint16 | Bit-field layout reference |
| [Sema Lisp](https://github.com/HelgeSverre/sema) | Same author; PIO instructions as S-expressions with named fields | The overall approach -- Fourth PIO is the concatenative version of the same idea |

---

## Build / Run / Test

```
make            # builds ./fourth
./fourth        # REPL
./fourth --demo # run demos
```

Oracle tests (requires [uv](https://docs.astral.sh/uv/)):

```
make test
```

---

## Word reference

### PIO instructions

| Word | Syntax | Description |
|------|--------|-------------|
| `pio-nop` | `pio-nop` | No-op (mov y y) |
| `pio-jmp` | `pio-jmp <label> [cond]` | Jump. Conditions: `always !x x-- !y y-- x!=y pin !osre` |
| `pio-wait` | `pio-wait <pol> <src> <idx> [rel]` | Wait. Sources: `gpio pin irq` |
| `pio-in` | `pio-in <src> <bits>` | Shift in. Sources: `pins x y null isr osr` |
| `pio-out` | `pio-out <dest> <bits>` | Shift out. Dests: `pins x y null pindirs pc isr exec` |
| `pio-push` | `pio-push` | Push ISR to RX FIFO |
| `pio-pull` | `pio-pull` | Pull TX FIFO to OSR |
| `pio-mov` | `pio-mov <dest> <src> [op]` | Move. Prefix `!` for invert, `reverse` for bit-reverse |
| `pio-irq` | `pio-irq <mode> <idx> [rel]` | IRQ. Modes: `set wait clear` |
| `pio-set` | `pio-set <dest> <val>` | Set. Dests: `pins x y pindirs` |

### Modifiers

| Word | Stack effect | Description |
|------|-------------|-------------|
| `delay` | `( instr n -- instr' )` | Set delay cycles (0-31) |
| `side` | `( instr n -- instr' )` | Set side-set value |

### Lists and transforms

| Word | Stack effect | Description |
|------|-------------|-------------|
| `[ ... ]` | `( -- list )` | Collect values into a list |
| `len` | `( list -- n )` | Length |
| `nth` | `( list n -- item )` | Index into list |
| `append` | `( list item -- list' )` | Append item |
| `concat` | `( list1 list2 -- list3 )` | Concatenate two lists |
| `map-delay` | `( list n -- list' )` | Set delay on every instruction |
| `filter-op` | `( list -- list' )` | Keep instructions matching opcode (parses name from input) |
| `label:` | parses name | Push label marker for assembler |

### Assembler

| Word | Stack effect | Description |
|------|-------------|-------------|
| `pio-assemble` | `( list -- program )` | Resolve labels, encode to machine code |
| `hexdump` | `( program -- )` | Print assembled words |

### Stack and arithmetic

`dup` `drop` `swap` `over` `rot` `+` `-` `*` `.` `.s` `.list` `cr` `\`

---

## How it works

Each PIO instruction is a C struct (`PioInstr`) with explicit named fields -- opcode, destination, source, bits, delay, side-set, condition, label. These structs live inside a tagged `Value` union on the Forth stack alongside integers, booleans, lists, and assembled programs.

The assembler (`pio-assemble`) takes a list of values, skips label markers, and encodes each `PioInstr` to a `uint16_t` using the RP2040 bit-field layout. Labels are resolved by scanning the list for string markers and counting instruction positions.

```
fourth.h       — Value types, PioInstr struct, VM state
fourth.c       — Interpreter, PIO words, assembler
main.c         — REPL and demo driver
test_fourth.c  — Outputs assembled hex as JSON
test_oracle.py — Compares against adafruit_pioasm
```

---

## Limitations

- No user-defined words (`: ... ;`). Dictionary is fixed at init.
- No control flow (`if/else/then`, `begin/until`, `do/loop`).
- Side-set bits hardcoded to 0. Programs using side-set will encode incorrectly.
- No `.wrap` / `.wrap_target` directives.
- Static list store (2,048 slots). Long REPL sessions will exhaust it.
- RP2040 only. No RP2350 extensions.
- Not tested on hardware.

## License

MIT
