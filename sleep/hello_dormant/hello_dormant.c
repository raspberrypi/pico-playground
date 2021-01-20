/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sleep.h"

int main() {
    stdio_init_all();
    printf("Hello Dormant!\n");

    printf("Switching to XOSC\n");
    uart_default_tx_wait_blocking();

    // UART will be reconfigured by sleep_run_from_xosc
    sleep_run_from_xosc();

    printf("Running from XOSC\n");
    uart_default_tx_wait_blocking();

    printf("XOSC going dormant\n");
    uart_default_tx_wait_blocking();

    // Go to sleep until we see a high edge on GPIO 10
    sleep_goto_dormant_until_edge_high(10);

    uint i = 0;
    while (1) {
        printf("XOSC awake %d\n", i++);
    }

    return 0;
}