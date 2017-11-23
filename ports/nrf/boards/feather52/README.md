# Setup

## Installing CircuitPython submodules

Before you can build, you will need to run the following commands once, which
will install the submodules that are part of the CircuitPython ecosystem, and
build the `mpy-cross` tool:

```
$ cd circuitpython
$ git submodule update --init
$ make -C mpy-cross
```

You then need to download the SD and Nordic SDK files via:

> This script relies on `wget`, which must be available from the command line.

```
$ cd ports/nrf
$ ./drivers/bluetooth/download_ble_stack.sh
```

## Installing `nrfutil`

The Adafruit Bluefruit nRF52 Feather ships with a serial and OTA BLE bootloader
that can be used to flash firmware images over a simple serial connection,
using the on-board USB serial converter.

If you haven't installed this command-line tool yet, go to the `/libs/nrfutil`
folder (where nrfutil 0.5.2 is installed as a sub-module) and run the following
commands:

> If you get a 'sudo: pip: command not found' error running 'sudo pip install',
you can install pip via 'sudo easy_install pip'

```
$ cd libs/nrfutil
$ sudo pip install -r requirements.txt
$ sudo python setup.py install
```

# Building and flashing firmware images

## Building CircuitPython binaries

#### REPL over UART (default settings)

To build a CircuitPython binary with default settings for the
`feather52` target enter:

> **NOTE:** `BOARD=feather52` is the default option and isn't stricly required.

```
$ make BOARD=feather52 V=1
```

#### REPL over BLE support

To build a CircuitPython binary with BLE support (S132) include `SD=s132`
as part of the build process:

```
$ make BOARD=feather52 V=1 SD=s132
```

## Flashing binaries with `nrfutil`

### 1. **Update bootloader** to single-bank version

The Adafruit nRF52 Feather ships, by default, with a **dual-bank** bootloader
that cuts the available flash memory in half in exchange for safer
OTA updates.

Due to the size of CircuitPython, we must migrate this bootloader to a
**single-bank** version, doubling the amount of flash memory available to us.

> These commands only need to be run once and will update the SoftDevice and
bootloader from the dual-bank version that ships on Arduino-based Adafruit
Feather52 boards to a single-bank CircuitPython compatible version:

#### S132 v2.0.1 single-bank (recommended):

By default s132 v2.0.1 is used when no `SOFTDEV_VERSION` field is passed in:

```
$ make BOARD=feather52 SERIAL=/dev/tty.SLAB_USBtoUART boot-flash
```

#### S132 v5.0.0 (BLE5, experimental):

To enable BLE5 support and the latest S132 release, flash the v5.0.0 bootloader via:

```
$ make BOARD=feather52 SERIAL=/dev/tty.SLAB_USBtoUART SOFTDEV_VERSION=5.0.0 boot-flash
```

### 2. Generate and flash a CircuitPython DFU .zip package over serial

The following command will package and flash the CircuitPython binary using the
appropriate bootloader mentionned above.

This command assumes you have already build a valid circuitpython
image, as described earlier in this readme.

> The name of the serial port target will vary, depending on your OS.

```
$ make BOARD=feather52 SERIAL=/dev/tty.SLAB_USBtoUART dfu-gen dfu-flash
```

If you built your CircuitPython binary with **BLE** support you will need to
add the `SD=s132` flag as shown below:

```
$ make BOARD=feather52 SERIAL=/dev/tty.SLAB_USBtoUART SD=s132 dfu-gen dfu-flash
```

## Working with CircuitPython

### Running local files with `ampy`

[ampy](https://learn.adafruit.com/micropython-basics-load-files-and-run-code/install-ampy)
is a command-line tool that can be used with the nRF52 Feather to transfer
local python files to the nRF52 for execution, rather than having to enter
the REPL manually, enter paste mode, and paste the code yourself.

> **IMPORTANT**: You must have `ampy` version **1.0.3** or higher to use `ampy`
  with the nRF52. The bootloader on the nRF52 requires a delay between the
  HW reset, and the moment when the command sequance is sent to enter raw
  mode. This required `-d/--delay` flag was added in release 1.0.3.


Save the following file as `test.py`:

```
import board
import digitalio
import time

led = digitalio.DigitalInOut(board.LED2)
led.direction = digitalio.Direction.OUTPUT

while True:
    led.value = True
    time.sleep(0.5)
    led.value = False
    time.sleep(0.5)
```

Then run the saved file via ampy, updating the serial port as required:

```
$ ampy -p /dev/tty.SLAB_USBtoUART -d 1.5 run test.py
```

This should give you blinky at 1 Hz on LED2 (the blue LED on the nRF52 Feather).
