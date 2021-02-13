#!/bin/bash
set -ex

rm -f *.bin pack.uf2
./packtiles -sdf bgar5515 Lighthouse_at_sunrise_by_Frenchie_Smalls.png lighthouse.bin
./packtiles -sdf bgar5515 Stone_Mountain_by_Brad_Huchteman.png stone_mountain.bin
./packtiles -sdf bgar5515 Voss_by_fortuneblues.png voss.bin
cat *.bin > pack.bin
uf2conv -f pico -b 0x1003c000 pack.bin -o pack.uf2