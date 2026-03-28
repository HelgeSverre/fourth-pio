// test_fourth.c — Output assembled hex for comparison with adafruit_pioasm
// Usage: ./test_fourth
// Outputs JSON lines: {"name": "...", "fourth": ["0xNNNN", ...]}

#include "../fourth.h"
#include <stdio.h>
#include <string.h>

static void run_test(const char *name, const char *forth_code) {
    ForthVM vm;
    fourth_init(&vm);

    fourth_eval(&vm, forth_code);

    if (vm.error) {
        printf("{\"name\": \"%s\", \"error\": \"%s\"}\n", name, vm.error_msg);
        return;
    }

    // Top of stack should be a program (VAL_BYTES)
    if (vm.sp < 1) {
        printf("{\"name\": \"%s\", \"error\": \"empty stack\"}\n", name);
        return;
    }

    Value top = vm.stack[vm.sp - 1];
    if (top.type != VAL_BYTES) {
        printf("{\"name\": \"%s\", \"error\": \"top is not a program (type=%d)\"}\n", name, top.type);
        return;
    }

    printf("{\"name\": \"%s\", \"fourth\": [", name);
    for (int i = 0; i < top.as.program.length; i++) {
        if (i > 0) printf(", ");
        printf("\"0x%04x\"", top.as.program.words[i]);
    }
    printf("]}\n");
}

int main(void) {
    // ── Basic instructions ──────────────────────────────────────
    run_test("nop",
        "[ pio-nop ] pio-assemble");

    run_test("set_pindirs_1",
        "[ pio-set pindirs 1 ] pio-assemble");

    run_test("set_pins_0",
        "[ pio-set pins 0 ] pio-assemble");

    run_test("set_pins_1",
        "[ pio-set pins 1 ] pio-assemble");

    run_test("set_x_31",
        "[ pio-set x 31 ] pio-assemble");

    run_test("set_y_0",
        "[ pio-set y 0 ] pio-assemble");

    run_test("set_pindirs_0",
        "[ pio-set pindirs 0 ] pio-assemble");

    // ── Delays ──────────────────────────────────────────────────
    run_test("set_pins_1_delay_31",
        "[ pio-set pins 1 31 delay ] pio-assemble");

    run_test("set_pins_0_delay_15",
        "[ pio-set pins 0 15 delay ] pio-assemble");

    run_test("nop_delay_5",
        "[ pio-nop 5 delay ] pio-assemble");

    // ── JMP with labels ─────────────────────────────────────────
    run_test("jmp_unconditional",
        "[ label: top  pio-nop  pio-jmp top ] pio-assemble");

    run_test("jmp_x_dec",
        "[ label: loop  pio-nop  pio-jmp loop x-- ] pio-assemble");

    run_test("jmp_not_x",
        "[ label: loop  pio-nop  pio-jmp loop !x ] pio-assemble");

    run_test("jmp_not_y",
        "[ label: loop  pio-nop  pio-jmp loop !y ] pio-assemble");

    run_test("jmp_y_dec",
        "[ label: loop  pio-nop  pio-jmp loop y-- ] pio-assemble");

    run_test("jmp_x_ne_y",
        "[ label: loop  pio-nop  pio-jmp loop x!=y ] pio-assemble");

    run_test("jmp_pin",
        "[ label: loop  pio-nop  pio-jmp loop pin ] pio-assemble");

    run_test("jmp_not_osre",
        "[ label: loop  pio-nop  pio-jmp loop !osre ] pio-assemble");

    // ── IN ──────────────────────────────────────────────────────
    run_test("in_pins_1",
        "[ pio-in pins 1 ] pio-assemble");

    run_test("in_pins_32",
        "[ pio-in pins 32 ] pio-assemble");

    run_test("in_x_8",
        "[ pio-in x 8 ] pio-assemble");

    run_test("in_y_16",
        "[ pio-in y 16 ] pio-assemble");

    run_test("in_null_1",
        "[ pio-in null 1 ] pio-assemble");

    run_test("in_isr_1",
        "[ pio-in isr 1 ] pio-assemble");

    run_test("in_osr_1",
        "[ pio-in osr 1 ] pio-assemble");

    // ── OUT ─────────────────────────────────────────────────────
    run_test("out_pins_1",
        "[ pio-out pins 1 ] pio-assemble");

    run_test("out_pins_32",
        "[ pio-out pins 32 ] pio-assemble");

    run_test("out_x_1",
        "[ pio-out x 1 ] pio-assemble");

    run_test("out_y_8",
        "[ pio-out y 8 ] pio-assemble");

    run_test("out_null_32",
        "[ pio-out null 32 ] pio-assemble");

    run_test("out_pindirs_1",
        "[ pio-out pindirs 1 ] pio-assemble");

    run_test("out_pc_1",
        "[ pio-out pc 1 ] pio-assemble");

    run_test("out_isr_8",
        "[ pio-out isr 8 ] pio-assemble");

    run_test("out_exec_16",
        "[ pio-out exec 16 ] pio-assemble");

    // ── PUSH / PULL ─────────────────────────────────────────────
    run_test("push_block",
        "[ pio-push ] pio-assemble");

    run_test("pull_block",
        "[ pio-pull ] pio-assemble");

    // ── MOV ─────────────────────────────────────────────────────
    run_test("mov_x_y",
        "[ pio-mov x y ] pio-assemble");

    run_test("mov_y_x",
        "[ pio-mov y x ] pio-assemble");

    run_test("mov_pins_x",
        "[ pio-mov pins x ] pio-assemble");

    run_test("mov_x_isr",
        "[ pio-mov x isr ] pio-assemble");

    run_test("mov_osr_null",
        "[ pio-mov osr null ] pio-assemble");

    run_test("mov_x_invert_x",
        "[ pio-mov x !x ] pio-assemble");

    run_test("mov_x_reverse_y",
        "[ pio-mov x y reverse ] pio-assemble");

    run_test("mov_isr_osr",
        "[ pio-mov isr osr ] pio-assemble");

    run_test("mov_exec_y",
        "[ pio-mov exec y ] pio-assemble");

    // ── WAIT ────────────────────────────────────────────────────
    run_test("wait_1_gpio_0",
        "[ pio-wait 1 gpio 0 ] pio-assemble");

    run_test("wait_0_gpio_5",
        "[ pio-wait 0 gpio 5 ] pio-assemble");

    run_test("wait_1_pin_3",
        "[ pio-wait 1 pin 3 ] pio-assemble");

    run_test("wait_0_pin_0",
        "[ pio-wait 0 pin 0 ] pio-assemble");

    run_test("wait_1_irq_2",
        "[ pio-wait 1 irq 2 ] pio-assemble");

    run_test("wait_0_irq_0",
        "[ pio-wait 0 irq 0 ] pio-assemble");

    // ── IRQ ─────────────────────────────────────────────────────
    run_test("irq_set_0",
        "[ pio-irq set 0 ] pio-assemble");

    run_test("irq_wait_1",
        "[ pio-irq wait 1 ] pio-assemble");

    run_test("irq_clear_2",
        "[ pio-irq clear 2 ] pio-assemble");

    run_test("irq_set_0_rel",
        "[ pio-irq set 0 rel ] pio-assemble");

    // ── SET with different values ───────────────────────────────
    run_test("set_pins_31",
        "[ pio-set pins 31 ] pio-assemble");

    run_test("set_x_0",
        "[ pio-set x 0 ] pio-assemble");

    run_test("set_y_15",
        "[ pio-set y 15 ] pio-assemble");

    // ── Complete programs ───────────────────────────────────────
    run_test("blinky",
        "[ pio-set pindirs 1  label: loop  pio-set pins 1 31 delay  pio-set pins 0 31 delay  pio-jmp loop ] pio-assemble");

    run_test("ws2812_bitloop",
        "[ label: bitloop  pio-out x 1  pio-jmp bitloop !x  pio-set pins 0 ] pio-assemble");

    run_test("uart_tx_like",
        "[ pio-pull  pio-set pindirs 1  pio-set pins 0  label: bitloop  pio-out pins 1  pio-jmp bitloop x-- ] pio-assemble");

    run_test("shift_in_8",
        "[ label: loop  pio-wait 1 pin 0  pio-in pins 8  pio-wait 0 pin 0  pio-jmp loop ] pio-assemble");

    return 0;
}
