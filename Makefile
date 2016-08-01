PROGRAM=mw-fw-rtos
EXTRA_COMPONENTS=extras/stdin_uart_interrupt extras/sntp extras/mbedtls
EXTRA_CFLAGS += -D_DEBUG_MSGS
MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma -w

include $(HOME)/src/esp8266/esp-open-rtos/common.mk

.PHONY: cart boot blank_cfg
cart: firmware/mw-fw-rtos.bin
	@$(MDMAP) firmware/mw-fw-rtos.bin:0x2000

boot:
	@$(MDMAP) $(RBOOT_PREBUILT_BIN)

blank_cfg:
	@$(MDMAP) $(RBOOT_CONF):0x1000

