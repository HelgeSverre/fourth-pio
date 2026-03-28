#!/usr/bin/env python3
"""
Test oracle: compare Fourth PIO assembler output against adafruit_pioasm.

Runs ./test_fourth to get Fourth's encodings, then assembles the same programs
with adafruit_pioasm and compares word-for-word.
"""

import json
import subprocess
import sys
from adafruit_pioasm import assemble

# ── Test cases ───────────────────────────────────────────────────
# Each entry: (name, pioasm_source)
# Names must match those in test_fourth.c

TESTS = {
    # Basic instructions
    "nop": "nop",
    "set_pindirs_1": "set pindirs, 1",
    "set_pins_0": "set pins, 0",
    "set_pins_1": "set pins, 1",
    "set_x_31": "set x, 31",
    "set_y_0": "set y, 0",
    "set_pindirs_0": "set pindirs, 0",

    # Delays
    "set_pins_1_delay_31": "set pins, 1 [31]",
    "set_pins_0_delay_15": "set pins, 0 [15]",
    "nop_delay_5": "nop [5]",

    # JMP with labels
    "jmp_unconditional": "top:\n  nop\n  jmp top",
    "jmp_x_dec": "loop:\n  nop\n  jmp x-- loop",
    "jmp_not_x": "loop:\n  nop\n  jmp !x loop",
    "jmp_not_y": "loop:\n  nop\n  jmp !y loop",
    "jmp_y_dec": "loop:\n  nop\n  jmp y-- loop",
    "jmp_x_ne_y": "loop:\n  nop\n  jmp x!=y loop",
    "jmp_pin": "loop:\n  nop\n  jmp pin loop",
    "jmp_not_osre": "loop:\n  nop\n  jmp !osre loop",

    # IN
    "in_pins_1": "in pins, 1",
    "in_pins_32": "in pins, 32",
    "in_x_8": "in x, 8",
    "in_y_16": "in y, 16",
    "in_null_1": "in null, 1",
    "in_isr_1": "in isr, 1",
    "in_osr_1": "in osr, 1",

    # OUT
    "out_pins_1": "out pins, 1",
    "out_pins_32": "out pins, 32",
    "out_x_1": "out x, 1",
    "out_y_8": "out y, 8",
    "out_null_32": "out null, 32",
    "out_pindirs_1": "out pindirs, 1",
    "out_pc_1": "out pc, 1",
    "out_isr_8": "out isr, 8",
    "out_exec_16": "out exec, 16",

    # PUSH / PULL
    "push_block": "push block",
    "pull_block": "pull block",

    # MOV
    "mov_x_y": "mov x, y",
    "mov_y_x": "mov y, x",
    "mov_pins_x": "mov pins, x",
    "mov_x_isr": "mov x, isr",
    "mov_osr_null": "mov osr, null",
    "mov_x_invert_x": "mov x, !x",
    "mov_x_reverse_y": "mov x, ::y",
    "mov_isr_osr": "mov isr, osr",
    "mov_exec_y": "mov exec, y",

    # WAIT
    "wait_1_gpio_0": "wait 1 gpio 0",
    "wait_0_gpio_5": "wait 0 gpio 5",
    "wait_1_pin_3": "wait 1 pin 3",
    "wait_0_pin_0": "wait 0 pin 0",
    "wait_1_irq_2": "wait 1 irq 2",
    "wait_0_irq_0": "wait 0 irq 0",

    # IRQ
    "irq_set_0": "irq 0",
    "irq_wait_1": "irq wait 1",
    "irq_clear_2": "irq clear 2",
    "irq_set_0_rel": "irq 0 rel",

    # SET with different values
    "set_pins_31": "set pins, 31",
    "set_x_0": "set x, 0",
    "set_y_15": "set y, 15",

    # Complete programs
    "blinky": "set pindirs, 1\nloop:\n  set pins, 1 [31]\n  set pins, 0 [31]\n  jmp loop",
    "ws2812_bitloop": "bitloop:\n  out x, 1\n  jmp !x bitloop\n  set pins, 0",
    "uart_tx_like": "pull\n  set pindirs, 1\n  set pins, 0\nbitloop:\n  out pins, 1\n  jmp x-- bitloop",
    "shift_in_8": "loop:\n  wait 1 pin 0\n  in pins, 8\n  wait 0 pin 0\n  jmp loop",
}


def get_adafruit_encoding(name, source):
    """Assemble with adafruit_pioasm and return list of hex strings."""
    full_source = f".program test\n" + "\n".join(f"  {line}" for line in source.split("\n"))
    try:
        words = assemble(full_source)
        return [f"0x{w:04x}" for w in words]
    except Exception as e:
        return f"ERROR: {e}"


def main():
    # Build test runner
    print("Building test_fourth...", flush=True)
    result = subprocess.run(
        ["cc", "-Wall", "-Wextra", "-O2", "-std=c11", "-o", "tests/test_fourth",
         "tests/test_fourth.c", "fourth.c"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"BUILD FAILED:\n{result.stderr}")
        sys.exit(1)

    # Run Fourth assembler
    print("Running Fourth assembler...", flush=True)
    result = subprocess.run(["./tests/test_fourth"], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FOURTH FAILED:\n{result.stderr}")
        sys.exit(1)

    fourth_results = {}
    for line in result.stdout.strip().split("\n"):
        data = json.loads(line)
        fourth_results[data["name"]] = data

    # Compare
    passed = 0
    failed = 0
    errors = 0

    print("\n" + "=" * 70)
    print("FOURTH PIO vs ADAFRUIT_PIOASM COMPARISON")
    print("=" * 70 + "\n")

    for name, source in TESTS.items():
        adafruit = get_adafruit_encoding(name, source)

        if name not in fourth_results:
            print(f"  SKIP  {name} — not in Fourth output")
            continue

        fourth_data = fourth_results[name]

        if "error" in fourth_data:
            print(f"  ERR   {name}")
            print(f"        Fourth error: {fourth_data['error']}")
            errors += 1
            continue

        fourth = fourth_data["fourth"]

        if isinstance(adafruit, str):
            print(f"  ERR   {name}")
            print(f"        Adafruit error: {adafruit}")
            errors += 1
            continue

        if fourth == adafruit:
            print(f"  PASS  {name}")
            passed += 1
        else:
            print(f"  FAIL  {name}")
            print(f"        Fourth:   {fourth}")
            print(f"        Adafruit: {adafruit}")
            # Show bit-level diff for first mismatch
            for i, (f, a) in enumerate(zip(fourth, adafruit)):
                if f != a:
                    fv = int(f, 16)
                    av = int(a, 16)
                    print(f"        Word {i}: Fourth={f} ({fv:016b})")
                    print(f"                Adafruit={a} ({av:016b})")
                    print(f"                XOR={fv ^ av:016b}")
                    break
            if len(fourth) != len(adafruit):
                print(f"        Length mismatch: Fourth={len(fourth)}, Adafruit={len(adafruit)}")
            failed += 1

    # Summary
    total = passed + failed + errors
    print(f"\n{'=' * 70}")
    print(f"RESULTS: {passed}/{total} passed, {failed} failed, {errors} errors")
    print(f"{'=' * 70}")

    sys.exit(0 if failed == 0 and errors == 0 else 1)


if __name__ == "__main__":
    main()
