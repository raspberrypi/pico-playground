# wifi\_settings\_connect example

Example using the wifi\_settings\_connect library to connect to a WiFi hotspot.

The Flash storage location for hotspot details is specified in
`include/wifi_settings/wifi_settings_configuration.h`. It is at
`0x101fc000` (for Pico W) and `0x103fc000` (for Pico 2 W). This
"wifi-settings file" is a text file which can be edited with any text editor.
Here is an example of typical contents:
```
    ssid1=MyHomeWiFi
    pass1=mypassword1234
    ssid2=MyPhoneHotspot
    pass2=secretpassword
    country=GB
```
To add your WiFi details at this location, please [see the setup instructions in the pico-extras
repo](https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/wifi_settings_connect/doc/SETTINGS_FILE.md).

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

The example program will search for WiFi hotspots. If it finds a hotspot which
matches those configured in Flash, then it will automatically
connect to it and begin a UDP broadcast. The example program prints
its status to the USB serial port; connect with a terminal program
to see what it is doing.
