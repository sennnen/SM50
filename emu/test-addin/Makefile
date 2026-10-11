# SM50 emulator test add-in.  make [SH_PREFIX=/opt/sh-elf/bin/sh-elf-] [OUT=build]
SH_PREFIX ?= /opt/sh-elf/bin/sh-elf-
CC      := $(SH_PREFIX)gcc
OBJCOPY := $(SH_PREFIX)objcopy
HERE    := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
OUT     ?= $(HERE)/build
TOOLS   := $(HERE)/../../tools
CFLAGS  := -m4-nofpu -mb -O2 -ffreestanding -nostdlib -fno-pie -fno-builtin -Wall -Wextra
LDFLAGS := -m4-nofpu -mb -nostdlib -fno-pie -T $(HERE)/addin.ld -Wl,--gc-sections -Wl,-Map,$(OUT)/test.map

all: $(OUT)/SM50TEST.g3a

$(OUT)/%.o: $(HERE)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%.o: $(HERE)/%.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/test.elf: $(OUT)/crt0.o $(OUT)/main.o $(OUT)/syscalls.o $(HERE)/addin.ld
	$(CC) $(LDFLAGS) -o $@ $(OUT)/crt0.o $(OUT)/main.o $(OUT)/syscalls.o -lgcc
$(OUT)/test.bin: $(OUT)/test.elf
	$(OBJCOPY) -O binary -R .bss $< $@
$(OUT)/SM50TEST.g3a: $(OUT)/test.bin
	python3 $(TOOLS)/mkg3a.py -n basic:SM50TEST -n internal:SM50TEST $< $@
	python3 $(TOOLS)/g3a_check.py $@ > /dev/null
$(OUT):
	mkdir -p $@
clean:
	rm -rf $(OUT)
.PHONY: all clean
