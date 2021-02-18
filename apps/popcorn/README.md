This is a 320x240x30 movie player that runs at a 48Mhz system clock (it was developed on FPGA which ran at that frequency).

The compression format is not surprisingly not crazily advanced, so movie files are large!

### Sample Movie

Here is "Big Buck Bunny": https://drive.google.com/file/d/1q3szTVccPZ08v_TMDxy9ZgqeOOXXwHCX/view?usp=sharing which is 1.6GB

### Using a movie

These are raw disk images without a filesystem. These instructions assume a certain level of knowledge Please feel free to submit PRs
to improve them!

#### Single Movie

A single movie can just be burned as the entirety of the SD card (via `dd` on unix). Note this will overwrite everything on the card.

#### One or More Movies

You can format the card with a GPT and then image movies onto the partitions (the partitions must obviously be big enough). The partition name from the GPT is used as the title for the movie.

### Converting

TODO - will upload a converter and instructions soon.