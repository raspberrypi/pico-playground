/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sleep.h"
#include "pico/aon_timer.h"

static bool awake;

static void sleep_callback(void) {
    printf("AON Timer woke us up\n");
    uart_default_tx_wait_blocking();
    awake = true;
}

static void aon_sleep(void) {

    // Get the time from the aon timer and set our alarm time
    struct timespec ts;
    aon_timer_get_time(&ts);
    ts.tv_sec += 10;

    printf("Sleeping for 10 seconds\n");
    uart_default_tx_wait_blocking();

    // Go to sleep
    sleep_goto_sleep_until(&ts, &sleep_callback);
}

int main() {

    stdio_init_all();

    printf("Hello AON timer Sleep!\n");

    struct timespec ts = { .tv_sec = 1723124088, .tv_nsec = 0 };
    aon_timer_start(&ts);

    do {
        printf("Awake for 10 seconds\n");
        sleep_ms(1000 * 10);

        printf("Switching to XOSC\n");

        // Wait for the fifo to be drained so we get reliable output
        uart_default_tx_wait_blocking();

        // Set the crystal oscillator as the dormant clock source, UART will be reconfigured from here
        // This is only really necessary before sending the pico into dormancy but running from xosc while asleep saves power
        sleep_run_from_xosc();

        // Go to sleep until an interrupt is generated after 10 seconds
        awake = false;
        aon_sleep();

        // Make sure we don't wake
        while (!awake) {
            printf("Should be sleeping\n");
        }

        // Re-enabling clock sources and generators.
        sleep_power_up();
    } while(true);

    return 0;
}
