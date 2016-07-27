# mw-fw-rtos
Firmare for the ESP8266 WiFi module installed in MegaWiFi cartridges. This firmware talks directly to the MegaWiFi API (mw-api) running on the Genesis/MegaDrive console, allowing it to connect to WiFi access points, and to send and receive data through The Internet using standard TCP and UDP protocols. There are some additional goodies provided by the firmware, like flash read/write functions and SNTP time synchronization.

# Building
Although building the firmware on Windows might be possible, Windows build environments are not supported. A GNU/Linux machine is recommended. This firmware uses [esp-open-rtos](https://github.com/SuperHouse/esp-open-rtos) framework. To build it first install `esp-open-rtos` and make sure the complete toolchain is working (e.g. by building the provided examples). Then edit the following lines of the `Makefile'
```
MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma -w

include $(HOME)/src/esp8266/esp-open-rtos/common.mk
```

To match the directories where you installed `esp-open-rtos `and `mw-mdma-cli` (optional, only if you want to flash the firmware using a MegaWiFi Programmer). Then cd to the path where the sources of this repository are located and simply run:

```
$ make
```

The recommended way of burning the firmware to the ESP8266 embedded on a MegaWiFi cartridge is using a MegaWiFi Programmer along with `mw-mdma-cli` tool. If you installed `mw-mdma-cli` tool and edited the path as instructed above, you can burn the firmware just by connecting the cart and the programmer and launching:
```
$ make cart
```

# Authors
This program has been written by doragasu.

# Contributions
Contributions are welcome. If you find a bug please open an issue, and if you have implemented a cool feature/improvement, please send a pull request.

# License
This program is provided with NO WARRANTY, under the [GPLv3 license](https://www.gnu.org/licenses/gpl-3.0.html). For the license terms of `esp-open-rtos`, [please read here](https://github.com/SuperHouse/esp-open-rtos/blob/master/README.md#licensing).

