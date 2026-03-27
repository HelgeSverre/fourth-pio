# Fourth PIO

A tiny Forth where RP2040 PIO instructions are first-class structured data — not raw integers, not strings, not opaque bytecode. You can build them, inspect them, transform them with `map`, `filter`, and `concat`, then assemble to correct machine code.

Built to prove a point: every survey of PIO tooling said "Forth could do this naturally but nobody has built it." Now somebody has.

## Build

```
make
```

## Usage

```
./fourth           # interactive REPL
./fourth --demo    # run the demo
```

## What it does

PIO instructions are structured values on the Forth stack:

```forth
pio-set pins 1 .          \ → {set pins 1}
pio-set pins 1 3 delay .  \ → {set pins 1 [3]}
pio-jmp loop !x .         \ → {jmp !x loop}
```

Collect them into lists, add labels, assemble:

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

### Structural manipulation

Transform every instruction in a program:

```forth
[ pio-set pins 1  pio-nop  pio-set pins 0  pio-nop ] 15 map-delay .list
\ → [ {set pins 1 [15]} {mov y y [15]} {set pins 0 [15]} {mov y y [15]} ]
```

Filter by opcode:

```forth
[ pio-set pindirs 1  pio-set pins 1  pio-nop  pio-jmp loop ] filter-op set .list
\ → [ {set pindirs 1} {set pins 1} ]
```

Compose program fragments:

```forth
[ pio-set pindirs 1 ] [ pio-set pins 1  pio-set pins 0 ] concat .list
\ → [ {set pindirs 1} {set pins 1} {set pins 0} ]
```

## Word reference

### PIO instructions

| Word | Stack effect | Description |
|------|-------------|-------------|
| `pio-nop` | `( -- instr )` | No-op (mov y y) |
| `pio-jmp` | `label [cond] ( -- instr )` | Jump. Conditions: `always !x x-- !y y-- x!=y pin !osre` |
| `pio-wait` | `pol src idx [rel] ( -- instr )` | Wait. Sources: `gpio pin irq` |
| `pio-in` | `src bits ( -- instr )` | Shift in. Sources: `pins x y null isr osr` |
| `pio-out` | `dest bits ( -- instr )` | Shift out. Dests: `pins x y null pindirs pc isr exec` |
| `pio-push` | `( -- instr )` | Push ISR to RX FIFO |
| `pio-pull` | `( -- instr )` | Pull TX FIFO to OSR |
| `pio-mov` | `dest src [op] ( -- instr )` | Move. Prefix `!` for invert. Ops: `invert reverse` |
| `pio-irq` | `mode idx [rel] ( -- instr )` | IRQ. Modes: `set wait clear` |
| `pio-set` | `dest val ( -- instr )` | Set. Dests: `pins x y pindirs` |

### Modifiers

| Word | Stack effect | Description |
|------|-------------|-------------|
| `delay` | `( instr n -- instr' )` | Set delay cycles |
| `side` | `( instr n -- instr' )` | Set side-set value |

### Lists & transforms

| Word | Stack effect | Description |
|------|-------------|-------------|
| `[ ... ]` | `( -- list )` | Collect values into a list |
| `len` | `( list -- n )` | List length |
| `nth` | `( list n -- item )` | Get nth item |
| `append` | `( list item -- list' )` | Append item |
| `concat` | `( list1 list2 -- list3 )` | Concatenate lists |
| `map-delay` | `( list n -- list' )` | Set delay on all instructions |
| `filter-op` | `op-name ( list -- list' )` | Keep instructions matching opcode |
| `label:` | `name ( -- str )` | Push a label marker for the assembler |

### Assembler

| Word | Stack effect | Description |
|------|-------------|-------------|
| `pio-assemble` | `( list -- program )` | Assemble instruction list to machine code |
| `hexdump` | `( program -- )` | Print assembled program |

### Stack & misc

`dup drop swap over rot + - * . .s .list cr \`

## Context

This exists because of a [comparison table](https://github.com/helgesverre/sema-lisp) of PIO tooling across embedded languages. The table noted that Forth could naturally treat PIO programs as structured data, but no implementation existed. ~850 lines of C later, it does.

## License

MIT
