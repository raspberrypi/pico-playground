# wifi\_settings\_connect example

Example using the wifi\_settings\_connect library to connect to
a WiFi hotspot. The library provides a way to store WiFi hotspot
details in Flash and connect automatically, avoiding the need to
specify build-time flags such as `WIFI_SSID` and `WIFI_PASSWORD`.

The Flash storage location for hotspot details is specified in
`include/wifi_settings/wifi_settings_configuration.h`. It is at
`0x101ff000` (for Pico W) and `0x103fe000` (for Pico 2 W). To add your
WiFi details at this location, please [see these
instructions](https://github.com/jwhitham/pico-wifi-settings/blob/master/doc/SETTINGS_FILE.md).
You can edit the settings as a text file and transfer it with `picotool`,
or install a [setup application](https://github.com/jwhitham/pico-wifi-settings/blob/master/doc/SETUP_APP.md)
to add or update WiFi details.

To build this example, first run `cmake` at the top level of the
`pico-playground` repository, specifying the locations for
the `pico-sdk` and `pico-extras` repositories, e.g.:
```
    cd /home/user/pico-playground
    mkdir build
    cd build
    cmake -DPICO_SDK_PATH=/home/user/pico-sdk \
        -DPICO_EXTRAS_PATH=/home/user/pico-extras \
        -DPICO_BOARD=pico_w ..
```
Then, run `make` within the `build/wifi_settings_connect/example`
directory, e.g.
```
    cd /home/user/pico-playground
    make -C build/wifi_settings_connect/example
```
This will create the UF2 file to be downloaded to the Pico
in `build/wifi_settings_connect/example`.

The wifi\_settings\_connect library in `pico-extras` is a subset of a larger
library, [wifi\_settings](https://github.com/jwhitham/pico-wifi-settings/),
which adds remote update functions for both the WiFi settings
and (optionally) your Pico application too.
