/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sleep.h"

static bool awake;

static void alarm_sleep_callback(uint alarm_id) {
    printf("alarm woke us up\n");
    uart_default_tx_wait_blocking();
    awake = true;
    hardware_alarm_set_callback(alarm_id, NULL);
    hardware_alarm_unclaim(alarm_id);
}

int main() {

    stdio_init_all();
    printf("Hello Alarm Sleep!\n");

    do {
        printf("Awake for 10 seconds\n");
        sleep_ms(1000 * 10);

        printf("Switching to XOSC\n");

        // Wait for the fifo to be drained so we get reliable output
        uart_default_tx_wait_blocking();

        // Set the crystal oscillator as the dormant clock source, UART will be reconfigured from here
        // This is only really necessary before sending the pico dormant but running from xosc while asleep saves power
        sleep_run_from_xosc();
        awake = false;

        // Go to sleep until the alarm interrupt is generated after 10 seconds
        printf("Sleeping for 10 seconds\n");
        uart_default_tx_wait_blocking();

        if (sleep_goto_sleep_for(10000, &alarm_sleep_callback)) {
            // Make sure we don't wake
            while (!awake) {
                printf("Should be sleeping\n");
            }
        }

        // Re-enabling clock sources and generators.
        sleep_power_up();
    } while(true);

    return 0;
}
