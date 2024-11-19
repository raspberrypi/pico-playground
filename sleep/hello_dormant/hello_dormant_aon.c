/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sleep.h"

// For clock_configure_gpin
#ifdef PICO_RP2040
#include "hardware/clocks.h"
#endif

// For RP2040 this example needs an external clock fed into the GP20
// Note: Only GP20 and GP22 can be used for clock input, See the GPIO function table in the datasheet.
// You can use another Pico to generate this. See the clocks/hello_gpout example for more details.
// rp2040: clock_gpio_init(21, CLOCKS_CLK_GPOUT3_CTRL_AUXSRC_VALUE_CLK_RTC, 1); // 46875Hz can only export a clock on gpios 21,23,24,25 and only 21 is exposed by Pico
// RP2350 has an LPOSC it can use, so doesn't need this
#define EXTERNAL_CLOCK_INPUT_PIN 20
#define RTC_FREQ_HZ 46875

static void sleep_callback(void) {
    printf("AON timer woke us up\n");
}

static void aon_sleep(void) {

    // Get the time from the aon timer and set our alarm time
    struct timespec ts;
    aon_timer_get_time(&ts);
    ts.tv_sec += 10;

    printf("Sleeping for 10 seconds\n");
    uart_default_tx_wait_blocking();

#if PICO_RP2040
    // The RTC must be run from an external source, since the dormant source will be inactive
    clock_configure_gpin(clk_rtc, EXTERNAL_CLOCK_INPUT_PIN, RTC_FREQ_HZ, 46875);
#endif

    // Go to sleep for 10 seconds, with RTC running off GP20
    // The external clock is the RTC of another pico being fed to GP20
    sleep_goto_dormant_until(&ts, &sleep_callback);
}

int main() {

    stdio_init_all();
    printf("Hello Dormant AON Timer!\n");

    struct timespec ts = { .tv_sec = 1723124088, .tv_nsec = 0 };
    aon_timer_start(&ts);

    while(true) {
        printf("Awake for 10s\n");
        sleep_ms(10000);

        uart_default_tx_wait_blocking();

        // Set the crystal oscillator as the dormant clock source, UART will be reconfigured from here
        // This is necessary before sending the pico dormant
#if PICO_RP2040
        printf("Switching to XOSC\n");
        sleep_run_from_xosc();
#else
        printf("Switching to LPSC\n");
        sleep_run_from_lposc();
#endif

        uart_default_tx_wait_blocking();

        printf("Going dormant\n");
        uart_default_tx_wait_blocking();

        // Go to sleep until the RTC interrupt is generated after 10 seconds
        aon_sleep();

        // Re-enabling clock sources and generators.
        sleep_power_up();
    }
    return 0;
}
