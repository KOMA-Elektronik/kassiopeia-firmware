# Kassiopeia Firmware

> Kassiopeia is a 4-channel DC interface that lets you control motors, solenoids, LEDs, and more with MIDI or CV/Gate.

> Tap, shake, vibrate, and move objects to create sound in ways you’ve never imagined. Build kinetic sound installations, trigger solenoids rhythmically, or even power-starve gear for experimental textures.

---

In this repo resides the firmware of the [Kassiopeia](https://koma-elektronik.com/new/product/kassiopeia/). Feel free to take a look at the source code and make suggestions on how to improve things or add features. In case you want to contribute and/or have feature requests, don't shy away from pull requests!

## Code structure

```bash
.
├── main.c
├── drivers/            # peripheral drivers
├── pico-sdk/           # offical repo as git submodule
├── system/             # software modules & system config
└── third-party/        # external libraries
```

## How to contribute

### Prerequisites

Make sure you have the following tools installed on your system:
- `cmake`
- `make`
- `openocd`
- `python3`
- `picotool` (optional, but nice to have)

For convienance, a `shell.nix` is provided for an easy (and reproducable) dev environment. It also comes with handy aliases.

### Clone

Clone the repo and its submodules:

```shell
$ git clone https://github.com/KOMA-Elektronik/kassiopeia-firmware.git
$ git submodule update --init
```

### Build and compile

Compile dependencies

```shell
$ mkdir build
$ cd build
$ cmake ..
```

Compile project
```shell
$ make kassiopeia
```

### Upload to hardware (easy mode)

If in-vito debug capabilities are not needed, use the [picotool](https://github.com/raspberrypi/picotool) to upload the binary straight into the flash.
Make sure the Kassiopeia is in bootloader mode!

```bash
$ picotool load build/kassiopeia.bin && picotool reboot
```

### Upload to hardware (hard mode)

Make sure that you have a probe that supports multicore uploading. Easiest is the [picoprobe](https://github.com/raspberrypi/picoprobe) where you use a RP Pico as debug probe. 

After, you can upoad the binary as follows:

```shell
$ sudo apt install openocd  # in case it is not installed yet
$ cd build                  # must be in build folder
$ openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program kassiopeia.elf verify reset exit" 
```

In case openocd does not find the rp2040.cfg file, build openocd from source.
```shell
cd ~/pico
sudo apt install automake autoconf build-essential texinfo libtool libftdi-dev libusb-1.0-0-dev
git clone https://github.com/raspberrypi/openocd.git --branch picoprobe --depth=1 --no-single-branch
cd openocd
./bootstrap
./configure --enable-picoprobe
make -j4
sudo make i
```

### Make a pull request

Create an issue to let us know that you are working on some changes. In case we want to support your changes, we gladly accept pull requests.


# License

The firmware of the Kassiopeia itself resides under the MIT license, however external software is attributed as follows:
- [pico-sdk](https://github.com/raspberrypi/pico-sdk) under the  BSD-3-Clause license
- [nanomidi](https://github.com/olemb/nanomidi) under the MIT license