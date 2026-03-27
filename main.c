// main.c — Fourth PIO: REPL and demo
// A Forth where PIO instructions are first-class structured data.

#include "fourth.h"
#include <stdio.h>
#include <string.h>

static void run(ForthVM *vm, const char *line) {
    fourth_eval(vm, line);
    if (vm->output[0]) printf("%s", vm->output);
    if (vm->error) printf("ERROR: %s\n", vm->error_msg);
}

static void demo(void) {
    ForthVM vm;
    fourth_init(&vm);

    printf("=== Fourth PIO — Structured PIO in Forth ===\n\n");

    // ── Demo 1: Build individual instructions and inspect them
    printf("--- 1. PIO instructions are structured data ---\n");
    run(&vm, "pio-set pindirs 1 .");
    printf("\n");
    run(&vm, "pio-set pins 0 .");
    printf("\n");
    run(&vm, "pio-nop .");
    printf("\n\n");

    // ── Demo 2: Compose with delay and side-set
    printf("--- 2. Composable modifiers ---\n");
    run(&vm, "pio-set pins 1 3 delay . cr");
    run(&vm, "pio-set pins 0 7 delay . cr");
    printf("\n");

    // ── Demo 3: Build a blinky program as a list
    printf("--- 3. Build a program as a list of structured instructions ---\n");
    run(&vm, "[ pio-set pindirs 1  label: loop  pio-set pins 1 31 delay  pio-set pins 0 31 delay  pio-jmp loop ] .list cr");
    printf("\n");

    // ── Demo 4: Assemble it!
    printf("--- 4. Assemble to machine code ---\n");
    run(&vm, "[ pio-set pindirs 1  label: loop  pio-set pins 1 31 delay  pio-set pins 0 31 delay  pio-jmp loop ] pio-assemble hexdump");
    printf("\n");

    // ── Demo 5: map-delay — transform all delays at once
    printf("--- 5. map-delay: transform all instructions ---\n");
    run(&vm, "[ pio-set pins 1  pio-nop  pio-set pins 0  pio-nop ] 15 map-delay .list cr");
    printf("\n");

    // ── Demo 6: filter — keep only certain opcodes
    printf("--- 6. filter-op: structural filtering ---\n");
    run(&vm, "[ pio-set pindirs 1  pio-set pins 1  pio-nop  pio-set pins 0  pio-jmp loop ] filter-op set .list cr");
    printf("\n");

    // ── Demo 7: concat — compose program fragments
    printf("--- 7. concat: compose fragments ---\n");
    run(&vm, "[ pio-set pindirs 1 ] [ pio-set pins 1 pio-set pins 0 ] concat .list cr");
    printf("\n");

    // ── Demo 8: A real WS2812 driver
    printf("--- 8. WS2812 LED driver (real PIO program) ---\n");
    run(&vm, "[ label: bitloop  pio-out x 1  pio-jmp bitloop !x  pio-set pins 0  pio-pull  pio-out null 32 ] pio-assemble hexdump");
    printf("\n");

    // ── Demo 9: Stack manipulation with PIO
    printf("--- 9. Stack manipulation with structured PIO ---\n");
    run(&vm, "pio-set pins 1  dup  .s cr");
    run(&vm, "drop drop");
    printf("\n");

    printf("=== Done! Forth can manipulate PIO as structured data. ===\n");
    printf("=== Cross that off the table. ===\n");
}

static void repl(void) {
    ForthVM vm;
    fourth_init(&vm);

    printf("Fourth PIO — A Forth for structured PIO programming\n");
    printf("Type 'bye' to exit. Use \\ for comments.\n\n");

    char line[1024];
    while (1) {
        printf("fourth> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        // Strip newline
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        if (strcmp(line, "bye") == 0) break;
        if (line[0] == '\0') continue;

        run(&vm, line);
        if (vm.output[0] && vm.output[vm.output_pos - 1] != '\n')
            printf("\n");
        printf(" ok\n");
    }
    printf("Bye!\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demo();
        return 0;
    }
    repl();
    return 0;
}
