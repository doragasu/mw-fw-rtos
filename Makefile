# mw-fw-rtos main Makefile

PROJECT_NAME := mw-fw-rtos

MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma
DD ?= /usr/bin/dd
TR ?= /usr/bin/tr
BOOT_BIN=build/bootloader/bootloader.bin
PART_BIN=build/partitions.bin
FW_BIN=$(PROJECT_NAME).bin

include $(IDF_PATH)/make/project.mk

.PHONY: cart boot blank_cfg partitions
cart: build/$(PROJECT_NAME).bin
	@$(MDMAP) -w $<:0x20000

boot:
	@$(MDMAP) -w $(BOOT_BIN)

# Wipes otadata, nv_cfg and cert partitions
blank_cfg: ones68k.bin
	@$(MDMAP) -w $<:0xF000

partitions:
	@$(MDMAP) -w $(PART_BIN):0x8000

ones68k.bin:
	$(DD) if=/dev/zero bs=68k count=1 | $(TR) "\000" "\377" > $@
