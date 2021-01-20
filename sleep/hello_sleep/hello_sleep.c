/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sleep.h"

#include "hardware/rtc.h"

static bool awake;

static void sleep_callback(void) {
    printf("RTC woke us up\n");
    awake = true;
}

static void rtc_sleep(void) {
    // Start on Friday 5th of June 2020 15:45:00
    datetime_t t = {
            .year  = 2020,
            .month = 06,
            .day   = 05,
            .dotw  = 5, // 0 is Sunday, so 5 is Friday
            .hour  = 15,
            .min   = 45,
            .sec   = 00
    };

    // Alarm 30 seconds later
    datetime_t t_alarm = {
            .year  = 2020,
            .month = 06,
            .day   = 05,
            .dotw  = 5, // 0 is Sunday, so 5 is Friday
            .hour  = 15,
            .min   = 45,
            .sec   = 10
    };

    // Start the RTC
    rtc_init();
    rtc_set_datetime(&t);

    printf("Sleeping for 10 seconds\n");
    uart_default_tx_wait_blocking();

    sleep_goto_sleep_until(&t_alarm, &sleep_callback);
}

int main() {
    stdio_init_all();
    printf("Hello Sleep!\n");

    printf("Switching to XOSC\n");

    // Wait for the fifo to be drained so we get reliable output
    uart_default_tx_wait_blocking();

    // UART will be reconfigured by sleep_run_from_xosc
    sleep_run_from_xosc();

    printf("Switched to XOSC\n");

    awake = false;

    rtc_sleep();

    // Make sure we don't wake
    while (!awake) {
        printf("Should be sleeping\n");
    }

    return 0;
}