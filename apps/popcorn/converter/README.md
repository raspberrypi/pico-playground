# Building

Note these instructions apply to Unix; they have not been tested on other platforms

To build the converter, either do a `PICO_PLATFORM=host` build of pico-playground (which will include it), 
or from this directory just do

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Converter takes two inputs (and you're going to need to read the `ffmpeg` docs!)

# .rgb file

This must be 320x240 30fps 24 bit raw RGB

e.g. 

```
ffmpeg -i input.mkv -vf "crop=1088:816:416:132, scale=(iw*sar)*max(320/(iw*sar)\,240/ih):ih*max(320/(iw*sar)\,240/ih)" -c:v rawvideo -pix_fmt rgb24 -r 30 movie.rgb
```

`-c:v rawvideo -pix_fmt rgb24 -r 30 movie.rgb` are key here (and the 320x240 output size)

# .pcm file

This must be stereo 44100Hz signed 16 bit little-endian raw data

e.g.

```
ffmpeg -i input.mkv -ac 2 -f s16le -c:a pcm_s16le -ar 44100 movie.pcm
```

# Converting

You can then do:

```c
converter movie.rgb movie.pcm movie.pl2
```

If the inputs are not as specified, then the converter will likely crash!
