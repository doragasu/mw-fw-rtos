# mw-fw-rtos main Makefile

PROJECT_NAME := mw-fw-rtos

MDMAP ?= $(HOME)/src/github/mw-mdma-cli/mdma
DD ?= /usr/bin/dd
TR ?= /usr/bin/tr
BOOT_BIN=build/bootloader/bootloader.bin
PART_BIN=build/partitions.bin
FW_BIN=$(PROJECT_NAME).bin

include $(IDF_PATH)/make/project.mk

.PHONY: cart boot blank_cfg partitions ota1 blank_otadata
ota1: build/$(PROJECT_NAME).bin
	@$(MDMAP) -w $<:0x110000

cart: build/$(PROJECT_NAME).bin
	@$(MDMAP) -w $<:0x10000

boot:
	@$(MDMAP) -w $(BOOT_BIN)

blank_otadata: ones8k.bin
	@$(MDMAP) -w $<:0xe000

# Wipes nv_cfg and cert partitions
blank_cfg: ones64k.bin
	@$(MDMAP) -w $<:0x100000

partitions:
	@$(MDMAP) -w $(PART_BIN):0x8000

ones8k.bin:
	$(DD) if=/dev/zero bs=8k count=1 | $(TR) "\000" "\377" > $@

ones64k.bin:
	$(DD) if=/dev/zero bs=64k count=1 | $(TR) "\000" "\377" > $@
