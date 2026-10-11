# fx-CG50 add-in build (included from the top-level Makefile).
SH_PREFIX ?= /opt/sh-elf/bin/sh-elf-
SH_CC := $(SH_PREFIX)gcc
SH_OBJCOPY := $(SH_PREFIX)objcopy
CALC_DIR := build/calc
CALC_CFLAGS := -m4-nofpu -mb -O2 -ffreestanding -nostdlib -fno-pie -fno-builtin-printf -std=c99 \
	-Wall -Wno-unused-parameter -Isrc/platform/casio/include -ffunction-sections -fdata-sections $(CALC_EXTRA)
CALC_SRC := $(ENGINE) src/platform/casio/main.c src/platform/casio/libc_min.c
CALC_ASM := src/platform/casio/crt0.S src/platform/casio/syscalls.S
CALC_NAME ?= SM50

calc: $(CALC_DIR)/$(CALC_NAME).g3a

$(CALC_DIR)/$(CALC_NAME).elf: $(CALC_SRC) $(CALC_ASM) src/platform/casio/pack.S src/platform/casio/prizm.ld content/sm50.pack src/engine/*.h
	@mkdir -p $(CALC_DIR)
	$(SH_CC) $(CALC_CFLAGS) -T src/platform/casio/prizm.ld -Wl,--gc-sections -Wl,-Map,$(CALC_DIR)/$(CALC_NAME).map \
		-DSM50_PACK_PATH='"content/sm50.pack"' -o $@ $(CALC_ASM) src/platform/casio/pack.S $(CALC_SRC) -lgcc

$(CALC_DIR)/$(CALC_NAME).bin: $(CALC_DIR)/$(CALC_NAME).elf
	$(SH_OBJCOPY) -O binary -R .bss $< $@

$(CALC_DIR)/$(CALC_NAME).g3a: $(CALC_DIR)/$(CALC_NAME).bin tools/mkg3a.py
	python3 tools/mkg3a.py -n "basic:SM50" $(if $(wildcard assets/icon_uns.png),-i uns:assets/icon_uns.png -i sel:assets/icon_sel.png) $< $@
	python3 tools/g3a_check.py $@ >/dev/null
