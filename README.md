# mw-fw-rtos
Firmare for the ESP8266 WiFi module installed in MegaWiFi cartridges. This firmware talks directly to the MegaWiFi API (mw-api) running on the Genesis/MegaDrive console, allowing it to connect to WiFi access points, and to send and receive data through The Internet using standard TCP and UDP protocols. There are some additional goodies provided by the firmware, like flash read/write functions and SNTP time synchronization.

# Building
Although building the firmware on Windows might be possible, Windows build environments are not supported. A GNU/Linux machine is recommended. This firmware uses [esp-open-rtos](https://github.com/SuperHouse/esp-open-rtos) framework, but as the original project (as of today) lacks the ability to switch the UART used for debugging, you will have to use [my fork (branch `devel-uart-sel`)](https://github.com/doragasu/esp-open-rtos/tree/devel-uart-sel).

So first clone my `esp-open-rtos` repository fork, switch to branch `devel-uart-sel` and follow the instructions to install the complete toolchain. Make sure it is working (e.g. by building the provided examples). Then edit the following lines of the `Makefile'
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

# Status
This is work in progress. Right now only a very limited amount of commands are working. Please be patient, and if you have the will and the skills, contribute!

# Authors
This program has been written by doragasu.

# Contributions
Contributions are welcome. If you find a bug please open an issue, and if you have implemented a cool feature/improvement, please send a pull request.

# License
This program is provided with NO WARRANTY, under the [GPLv3 license](https://www.gnu.org/licenses/gpl-3.0.html). For the license terms of `esp-open-rtos`, [please read here](https://github.com/SuperHouse/esp-open-rtos/blob/master/README.md#licensing).

