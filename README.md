# mcp3008hwspi

This is a simple command line tool for Raspberry Pi interfacing with Microchip's MCP3008 ADC ([datasheet][mcp3008_datasheet]) over SPI interface. It achieves sampling rates exceeding 100kHz with a slight modification of a stock raspberry kernel.

Even without kernel modification, higher sampling rates (up to ~63 kHz on Raspberry Pi 3B+) can be achieved when compared to using Raspberry Pi's [MCP3008 kernel driver][mcp3008_kernel_driver] or [bit-banging via pigpio][mcp3008_pigpio].

The approach is based on [this post][jumpnowtek_post] and [this implementation][jumpnowtek_repo] describing how to reduce SPI transaction roundtrip per sample.

- [Usage](#usage)
- [Compilation](#compilation)
- [Wiring and testing](#wiring-and-testing)
- [Benchmarks](#benchmarks)
- [SPI kernel driver patch](#spi-kernel-driver-patch)

## Usage

```
mcp3008hwspi (version 0.0.1)
Reads data from MCP3008 ADC through hardware SPI interface on Raspberry Pi.
Online help, docs & bug reports: <https://github.com/nagimov/mcp3008hwspi>

Usage: mcp3008hwspi [OPTION]...
Mandatory arguments to long options are mandatory for short options too.
  -b, --block B        read B blocks per every scan of all specified channels,
                       1 <= BPR <= 511 (default: 1) [integer];
                       multiple channels are always read as a single block;
  -r, --clockrate CR   SPI clock rate, Hz, 1000000 <= CR <= 3600000
                       (default: 3600000) [integer];
                       MCP3008 must be powered from 5V for 3.6MHz clock rate;
  -c, --channels CH    read specified channels CH, 0 <= CH <= 7 (default: 0);
                       multiple channels can be specified, e.g. -c 0123;
                       all channels are read as a single block, e.g. if ran as
                       <mcp3008hwspi -c 0123 -b 2>
                       8 blocks are transmitted per SPI read (4 channels x 2);
  -s, --save FILE      save data to specified FILE (if not specified, data is
                       printed to stdout);
  -n, --samples N      set the number of samples per channel to be read to N
                       (default: 1000 samples) [integer];
  -f, --freq FREQ      set the sampling rate to FREQ, samples per second
                       (default: 0 Hz) [integer];
                       if set to 0, ADC is sampled at maximum achievable rate,
                       if set to > 0, --block is reset to 1;

Data is streamed in comma separated format, e. g.:
  sample ch0,  value ch0,  sample ch1,  value ch1
           0,       1023,           1,        512
           2,       1022,           3,        513
         ...,        ...,         ...,        ...
  samples are (hopefully) equally spaced in time;
  channels are read sequentially with equal time delays between samples;
  value chX shows raw 10-bit integer readback from channel X;
  average sampling rate is written to both stdout and output file header.

Exit status:
  0  if OK
  1  if error occurred while reading or wrong cmdline arguments.

Example:
  mcp3008hwspi  -r 3600000  -c 0123  -s out.csv  -f 0  -n 1000  -b 25
                      ^         ^         ^        ^      ^        ^
                      |         |         |        |      |        |
  3.6 MHz SPI clock --+         |         |        |      |        |
  read channels 0, 1, 2 and 3 --+         |        |      |        |
  save data to output file 'out.csv' -----+        |      |        |
  set sampling frequency to max achievable rate ---+      |        |
  read 1000 samples per channel (1000 x 4 = 4000 total) --+        |
  read channels in blocks of 25 (25 x 4 = 100 blocks per SPI read)-+
```

**Notes:**

- 5V supply is required for 3.6 MHz clock rate (max clock rate at 3.3V supply is ~1.0 MHz);
- sampling rate is integrated over all channels (e.g. 100 kHz for one channel ~= 50 kHz for two channels);
- when multiple channels are specified, all channels are always read as a single block, e.g. `-c 01 -b 20` means both channels 0 and 1 will be read in blocks of 20 samples each, 40 blocks per SPI transaction in total;
- try experimenting with the block size if sampling rate is important to you (see [Block size and sampling rate](#block-size-and-sampling-rate)).

### Block size and sampling rate

Parameter `-b` defines number of reads batched to a single SPI transaction. When `mcp3008hwspi` is used with a patched kernel (see [SPI kernel driver patch](#spi-kernel-driver-patch)), optimal value of `-b` is somewhere around `100`. Note that when more than a single channel is specified, actual number of samples received per transaction is multiplied by a number of channels, since all the channels are always read as a single block. E.g. in order to read the first five channels (e.g. `-c 01234`), optimal value of the block size is expected to be around `20`.

When unmodified raspberry kernel is used, optimal value of parameter `-b` is `1`. Since it is a default value, in this case it can be safely omitted.

## Compilation

No external dependencies or configurations required:

```
git clone https://github.com/nagimov/mcp3008hwspi
cd mcp3008hwspi
make
sudo make install
```

**Build is only tested on Raspbian OS.**

## Wiring and testing

Any audio DAC can be used as a simple signal generator for testing a kHz-range sampling rate ADC. There are plenty of PC/smartphone applications for this purpose (for the following examples [Function Generator][function_generator_app] app is used).

Simple fritzing diagram for testing purposes:

![MCP3008][mcp3008_diagram]

- MCP3008 is powered (VDD) from 5V but referenced (VREF) from 3.3V in order to increase bit-per-volt resolution for a small amplitude audio signal;
- 2 x 100 kOhm resistors create a voltage divider in order to bias audio input by 1.65V;
- 220 Ohm resistor limits inrush currents to capacitors;
- fairly high bypass capacitance is recommended on both VDD and VREF lines ([datasheet][mcp3008_datasheet] recommends 1 μF, however readings are more stable at 2.2 μF, possibly due to Raspberry Pi's voltage regulation);
- right and left audio channels are connected to channels 0 and 1 of MCP3008, bias voltage is connected to channel 2.

I use python with numpy and matplotlib to easily read and visualize data. Save this script as `plot.py`.

```python
import sys
import numpy as np
import matplotlib.pyplot as plt
plt.figure(figsize=(9,2))
data = np.genfromtxt(sys.argv[1], skip_header=1, delimiter=',', names=True)
cols = iter(data.dtype.names)
for s, v in zip(cols, cols):
    plt.plot(data[s], data[v], '.-', label='ch %s' % [int(d) for d in s if d.isdigit()][0], linewidth=0.5, markersize=1)
plt.tight_layout()
plt.legend(loc='upper right')
plt.savefig('plot.png', dpi=100)  # or plt.show()
```

![setup][mcp3008_setup_photo]

### Destructive interference

For the first test, [Function Generator][function_generator_app] is set to output two 1 kHz sine wave signals from the right and left channels, with a phase difference of 180 degrees. Due to destructive interference of sound waves, there will be no sound coming out of the smartphone speaker with the audio cable unplugged. However signals are still there:

- read three channels (right, left and bias), 70 samples each:
```
$ mcp3008hwspi -c 012 -f 0 -n 70 -s out-1kHz.csv -b 10
0.00 seconds, 210 samples, 103092.78 Hz total sample rate, 34364.26 Hz per-channel sample rate
Writing to the output file...
```

- at ~35 kHz per channel, there should be approximately `70samples/(35kHz/1kHz)=2` full periods of each sine wave:

```
python3 plot.py out-1kHz.csv
```

![plot-1kHz][plot_1kHz]

### Frequency sweep

For the second test, [Function Generator][function_generator_app] is set to output a frequency sweep from 20 Hz to 20 kHz, mode - bounce, time - 0.01s. Due to a limited frequency bandwidth of simple audio DAC, signal roll-off can be expected at higher frequencies:

- read 2000 samples from channel 0:
```
$ mcp3008hwspi -c 0 -f 0 -n 2000 -s out-sweep.csv -b 100
0.02 seconds, 2000 samples, 104613.45 Hz total sample rate, 104613.45 Hz per-channel sample rate
Writing to the output file...
```

- at ~100 kHz per channel, there should be approximately `2000/100kHz/0.01s=2` frequency transitions (from high to low and from low to high):

```
python3 plot.py out-sweep.csv
```

![plot-sweep][plot_sweep]

## Benchmarks

Achievable sampling rate depends on multiple factors (Raspberry Pi model, OS version, CPU load during the benchmark, etc.). For the following test cases, `mcp3008hwspi` is ran three times and the second best result is recorded. When using a modified kernel, a block size of 100 is used for a single channel and a block size of 12 is used for eight channels (`100/8=~12`).

If you have more benchmarking data, please share via PR or submit an issue.

- single channel, Raspbian Buster Lite (2020-02-12), stock kernel, Raspberry Pi 3B+:
```
$ mcp3008hwspi -c 0 -f 0 -n 1000000 -s out.csv
15.74 seconds, 1000000 samples, 63522.65 Hz total sample rate, 63522.65 Hz per-channel sample rate
Writing to the output file...
```

- eight channels, Raspbian Buster Lite (2020-02-12), stock kernel, Raspberry Pi 3B+:
```
$ mcp3008hwspi -c 01234567 -f 0 -n 100000 -s out.csv
15.39 seconds, 800000 samples, 51985.33 Hz total sample rate, 6498.17 Hz per-channel sample rate
Writing to the output file...
```

- single channel, Raspbian Buster Lite (2020-02-12), modified kernel, Raspberry Pi 3B+:
```
$ mcp3008hwspi -c 0 -f 0 -n 1000000 -s out.csv -b 100
9.57 seconds, 1000000 samples, 104467.59 Hz total sample rate, 104467.59 Hz per-channel sample rate
Writing to the output file...
```

- eight channels, Raspbian Buster Lite (2020-02-12), modified kernel, Raspberry Pi 3B+:
```
$ mcp3008hwspi -c 01234567 -f 0 -n 100000 -s out.csv -b 12
7.66 seconds, 800064 samples, 104500.97 Hz total sample rate, 13062.62 Hz per-channel sample rate
Writing to the output file...
```

## SPI kernel driver patch

You only need to follow this part if higher sampling rates are required compared to a stock raspberry kernel. Don't be discouraged by a scary "kernel thing" - the process is straightforward and well documented.

**Note: the following applies to kernel version 4.19. Check [this page][raspberry_kernel_building_instructions] for the latest kernel building instructions.**

### What needs to be changed

MCP3008's sampling rate is limited to 200 kHz, with its SPI clock rate defined as `18*f_SAMPLE`, transferring 18 bits per sample at a maximum clock rate `18*200kHz=3.6MHz` (see more info in the [datasheet][mcp3008_datasheet]). However SPI driver of raspberry kernel communicates using 8-bit words (can be seen [here][spi_kernel_8bit_word]) and wastes a clock cycle per every byte transmitted (can be seen [here][spi_kernel_clock_cycle]), so maximum theoretical sampling rate is down to `3.6MHz/(3*(8+1))=133.3kHz`. Another large inefficiency comes from a 10 us delay introduced after every single toggle of CS line defined in a core `spi.c` driver (can be seen [here][spi_kernel_delay]). Fortunately, this delay can be removed entirely without affecting SPI functionality when running at 3.6 MHz clock rate. This change alone increases sampling rate up to >100 kHz.

Note: this modification will very likely affect SPI communication with other devices, especially at higher clock rates. A full explanation including math and timing calculations is given in [this post][jumpnowtek_post].

### Step by step instructions

It takes a couple of hours to compile 4.19 kernel on a Raspberry Pi 3B+. You can use a linux workstation or VM to speed things up - manuals for cross-compiling can be found [here][raspberry_kernel_building_instructions]. If you are using Raspberry Pi 2 or older, cross-compiling is probably a better option.

For simplicity and portability reasons, the following instructions are only provided for building the kernel locally on a Raspberry Pi itself. Make sure to provide sufficient CPU cooling, especially for Pi 4 models - full CPU load is known to cause overheating.

- install required packages:
```
sudo apt-get update
sudo apt-get install git bc bison flex libssl-dev make
```

- clone raspberry kernel:
    + make sure to include `--depth=1` to prevent git from copying the entire history
    + make sure to change `--branch rpi-4.19.y` to the current version of your raspberry kernel (run `uname -r` to display the kernel version)
    + git will fetch around 200 MB and unpack it, depending on your connectivity this might take 10-20 minutes
```
git clone --depth=1 --branch rpi-4.19.y https://github.com/raspberrypi/linux
```

- modify `spi.c` driver (get rid of the above mentioned 10 us delay) and make sure that the line is commented:
```
sed -i "s/udelay(10);/\/\/udelay(10);/" linux/drivers/spi/spi.c
cat linux/drivers/spi/spi.c | grep "udelay(10);"
```

Kernel configuration commands are dependent on the model of Raspberry Pi. This is a relatively quick step (takes under a minute):

- for Pi 1, Pi Zero, Pi Zero W, or Compute Module:
```
cd linux
KERNEL=kernel
make bcmrpi_defconfig
```

- for Pi 2, Pi 3, Pi 3+ or Compute Module 3:
```
cd linux
KERNEL=kernel7
make bcm2709_defconfig
```

- for Pi 4:
```
cd linux
KERNEL=kernel7l
make bcm2711_defconfig
```

Next step is building kernel modules. Grab a book - this will take a couple of hours on Raspberry Pi 3B+:

```
make -j4 zImage modules dtbs
```

Install compiled modules (another ~3 minutes):

```
sudo make modules_install
```

Final step is to copy everything onto a boot partition:

```
sudo cp arch/arm/boot/dts/*.dtb /boot/
sudo cp arch/arm/boot/dts/overlays/*.dtb* /boot/overlays/
sudo cp arch/arm/boot/dts/overlays/README /boot/overlays/
sudo cp arch/arm/boot/zImage /boot/$KERNEL.img
```

Reboot the system to a newly built kernel.


[mcp3008hwspi_binary]: mcp3008hwspi
[mcp3008_datasheet]: http://ww1.microchip.com/downloads/en/DeviceDoc/21295d.pdf
[mcp3008_kernel_driver]: https://github.com/raspberrypi/linux/blob/rpi-4.4.y/arch/arm/boot/dts/overlays/mcp3008-overlay.dts
[mcp3008_pigpio]: http://abyz.me.uk/rpi/pigpio/code/rawMCP3008_c.zip
[jumpnowtek_post]: https://jumpnowtek.com/rpi/Analyzing-raspberry-pi-spi-performance.html
[jumpnowtek_repo]: https://github.com/scottellis/mcp3008-speedtest
[function_generator_app]: https://play.google.com/store/apps/details?id=com.keuwl.functiongenerator
[spi_kernel_8bit_word]: https://github.com/raspberrypi/linux/blob/2e79fd01b4b9a7eea5acb234ad4e4cdca8449d5a/drivers/spi/spi-bcm2835.c#L723
[spi_kernel_clock_cycle]: https://github.com/raspberrypi/linux/blob/2e79fd01b4b9a7eea5acb234ad4e4cdca8449d5a/drivers/spi/spi-bcm2835.c#L582
[spi_kernel_delay]: https://github.com/raspberrypi/linux/blob/2e79fd01b4b9a7eea5acb234ad4e4cdca8449d5a/drivers/spi/spi.c#L1091
[raspberry_kernel_building_instructions]: https://www.raspberrypi.org/documentation/linux/kernel/building.md

[mcp3008_diagram]: img/diagram.png
[mcp3008_setup_photo]: img/setup.jpg
[plot_1kHz]: img/plot-1kHz.png
[plot_sweep]: img/plot-sweep.png
