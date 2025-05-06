/**
 * Copyright (c) 2025 Jack Whitham
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Example for wifi-settings.
 *
 * This example connects to WiFi using wifi_settings_connect functions,
 * and then broadcasts a message on UDP port 1234 every second.
 * You can receive these with any tool that can receive
 * UDP, e.g. tcpdump, Wireshark, or netcat:
 *
 *   nc -l -u -p 1234
 *
 * The WiFi connection details must be configured in Flash as described here:
 * https://github.com/jwhitham/pico-wifi-settings/blob/master/doc/SETTINGS_FILE.md
 */


#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "wifi_settings/wifi_settings_connect.h"
#include "wifi_settings/wifi_settings_hostname.h"


bool send_udp_packet(uint count) {
    // Send a UDP broadcast to port 1234
    char text[256];
    bool ok = false;

    struct udp_pcb* udp_pcb = udp_new();
    if (!udp_pcb) {
        printf("Failed to allocate space for UDP PCB!\n");
    } else {
        snprintf(text, sizeof(text), "Hello World %u from %s\n", count, wifi_settings_get_hostname());
        const uint size = strlen(text);
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
        if (!p) {
            printf("Failed to allocate space for UDP packet!\n");
        } else {
            memcpy(p->payload, text, size);
            ip_addr_t addr;
            ipaddr_aton("255.255.255.255", &addr);
            const err_t err = udp_sendto(udp_pcb, p, &addr, 1234);
            if (err) {
                printf("Failed to send UDP packet! error=%d\n", (int) err);
            } else {
                printf("UDP broadcast, port 1234: %s", text);
                ok = true;
            }
            pbuf_free(p);
        }
        udp_remove(udp_pcb);
    }
    return ok;
}

int main() {
    stdio_init_all();

    // Initialise pico-wifi-settings
    int rc = wifi_settings_init();
    if (rc != 0) {
        panic("wifi_settings_init() failed");
        return 1;
    }

    // Begin connecting to WiFi (this function returns immediately)
    wifi_settings_connect();

    uint count = 0;
    bool stay_in_loop = true;
    while (stay_in_loop) {
        // clear screen
        printf("\x1b[2J\r");

        // print host name and board ID
        printf("Hostname = %s\n"
               "Board ID = %s\n\n",
               wifi_settings_get_hostname(),
               wifi_settings_get_board_id_hex());
        fflush(stdout);

        // print connection status
        char text[512];
        wifi_settings_get_connect_status_text(text, sizeof(text));
        printf("%s\n\n", text);
        if (wifi_settings_has_no_wifi_details()) {
            // Help the user if no SSIDs are configured
            printf("You need to configure at least one hotspot! See\n"
                   "https://github.com/jwhitham/pico-wifi-settings/blob/master/doc/SETTINGS_FILE.md\n"
                   "for instructions.\n\n");
        } else {
            wifi_settings_get_hw_status_text(text, sizeof(text));
            printf("%s\n", text);
            wifi_settings_get_ip_status_text(text, sizeof(text));
            printf("%s\n", text);
        }
        fflush(stdout);

        // Send a UDP broadcast to port 1234 if connected
        if (wifi_settings_is_connected()) {
            if (send_udp_packet(count)) {
                count++;
            }
        }

        printf("press 'c' to connect, 'd' to disconnect, 'r' to return to bootloader\n");
        fflush(stdout);
        switch (getchar_timeout_us(1)) {
            case 'c':
                wifi_settings_connect();
                break;
            case 'd':
                wifi_settings_disconnect();
                break;
            case 'r':
                stay_in_loop = false;
                break;
            default:
                break;
        }
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer interrupt) to check for wifi_settings, WiFi driver
        // or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep:
        sleep_ms(1000);
#else
        // if you are not using pico_cyw43_arch_poll, then wifi_settings, WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }
    printf("That's all\n");

    // Disconnection and de-initialisation are optional steps
    // with wifi_settings, but you may wish to explicitly stop
    // the WiFi connection for some reason:
    wifi_settings_disconnect();
    wifi_settings_deinit();
    printf("Goodbye\n");
    // return to boot loader
    reset_usb_boot(0, 0);
    return 0;
}
