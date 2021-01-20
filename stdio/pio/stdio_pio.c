/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Example of how to attach a custom interface to the stdout plumbing, so that
// printf(), puts() etc will have their output directed there. This example
// uses PIO to add an extra UART output on GPIO 2, which is not normally
// usable for UART TX.

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "hardware/pio.h"
#include "uart_tx.pio.h"

#define SERIAL_TX_PIN 2
#define SERIAL_BAUD 115200

// Shim function for directing characters to a fixed SM.
PIO print_pio;
uint print_sm;
void pio_out_chars(const char *buf, int len) {
    for (int i = 0; i < len; ++i) {
        uart_tx_program_putc(print_pio, print_sm, buf[i]);
    }
}

// Data structure for registering this function with the stdio plumbing (we
// only provide output here, but stdin can be hooked up in the same way.)
stdio_driver_t stdio_pio = {
    .out_chars = pio_out_chars,
#ifdef PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};

int main() {

    stdio_init_all();
    // This text will go to all the regular stdout outputs enabled on this
    // build (any/all of UART, USB and semihosting)
    printf("PIO stdio example! PIO isn't set up yet.\n");

    // Get the state machine ready to print characters
    print_pio = pio0;
    print_sm = pio_claim_unused_sm(print_pio, true);
    uint offset = pio_add_program(print_pio, &uart_tx_program);
    uart_tx_program_init(print_pio, print_sm, offset, SERIAL_TX_PIN, SERIAL_BAUD);

    while (true) {
        // Register the print function with stdio
        stdio_set_driver_enabled(&stdio_pio, true);
        printf("\n\n1. PIO driver enabled -- this text should go to all outputs.\n");

        // Direct stdout *only* to PIO
        stdio_filter_driver(&stdio_pio);
        printf("2. PIO driver filtered -- this text should go *only* to PIO on GPIO %d\n", SERIAL_TX_PIN);

        // Remove filter to send text to all outputs once more
        stdio_filter_driver(NULL);
        printf("3. Filter removed -- this text should go to all outputs.\n");

        stdio_set_driver_enabled(&stdio_pio, false);
        printf("4. PIO driver removed -- this text should go only to the standard outputs.\n");

        sleep_ms(1000);
    }
}
