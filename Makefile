PROGRAM=mw-fw-rtos
EXTRA_COMPONENTS=extras/stdin_uart_interrupt extras/sntp extras/mbedtls
EXTRA_CFLAGS += -D_DEBUG_MSGS
MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma -w
DD ?= /usr/bin/dd
TR ?= /usr/bin/tr

include $(HOME)/src/esp8266/esp-open-rtos/common.mk

.PHONY: cart boot blank_cfg blank_wificfg
cart: firmware/$(PROGRAM).bin
	@$(MDMAP) $<:0x2000

boot:
	@$(MDMAP) $(RBOOT_BIN)
#	@$(MDMAP) $(RBOOT_PREBUILT_BIN)

blank_cfg:
	@$(MDMAP) $(RBOOT_CONF):0x1000

blank_wificfg: ones4k.bin
	@$(MDMAP) $<:0x7F000

ones4k.bin:
	$(DD) if=/dev/zero bs=4096 count=1 | $(TR) "\000" "\377" > $@

