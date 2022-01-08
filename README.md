# WARNING: THIS PROJECT HAS MOVED

You can find the most recent version at the [mw-fw-rtos GitLab project page](https://gitlab.com/doragasu/mw-fw-rtos). This repository will be kept as is, and will not be updated anymore.

# mw-fw-rtos

Firmare for the ESP8266 WiFi module installed in MegaWiFi cartridges. This firmware talks directly to the MegaWiFi API (mw-api) running on the Genesis/MegaDrive console, allowing it to connect to WiFi access points, and to send and receive data through The Internet using standard TCP and UDP protocols. There are some additional goodies provided by the firmware, like flash read/write functions and SNTP time synchronization.

# Building

This firmware is based on [ESP8266\_RTOS\_SDK](https://github.com/espressif/ESP8266_RTOS_SDK). Follow the SDK instructions to install the toolchain and build the firmware. Currently this project builds against `release/v3.4` branch, so make sure to check it out.

To burn the built firmware, edit the following line of the `Makefile`, and make sure it points to your installation of the mdma utility:
```
MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma
```

The recommended way of burning the firmware to the ESP8266 embedded on a MegaWiFi cartridge is using a MegaWiFi Programmer along with `mw-mdma-cli` tool. If you installed `mw-mdma-cli` tool and edited the path as instructed above, you can burn the firmware just by connecting the cart and the programmer to a PC. The first time you burn the firmware, you have to write the bootloader, the partitions and the firmware blob itself:

```
$ make boot
$ make partitions
$ make cart
```

Then, when you need to update the firmware, you just need to run the last command (`make cart`), no need to burn the boot and partitions again.

# Status

This firmware, paired with the `mw-api` running on a Megadrive/Genesis, allows the console to:

* Configure and associate to access points (including neighbor scan functions).
* Store up to 3 network configurations.
* Store up to 3 gamertags.
* Use TCP and UDP for transport.
* Create both client and server sockets.
* Perform HTTP/HTTPS requests.
* Synchronize the date/time from NTP servers.
* Generate random numbers blazingly fast.
* Store and read custom data on non volatile flash (up to 16 megabits are available in addition to the standard 32 megabits of the cart).
* Integrate with GameJolt for online scoreboards, trophies, user and friend management, cloud storage, etc.

# Authors

This program has been written by doragasu. This is part of [MegaWiFi project](https://github.com/doragasu/mw).

# Contributions

Contributions are welcome. If you find a bug please open an issue, and if you have implemented a cool feature/improvement, please send a pull request.

# License

This program is provided with NO WARRANTY, under the [GPLv3 license](https://www.gnu.org/licenses/gpl-3.0.html). Make sure you also check `ESP8266_RTOS_SDK` for its license terms.

