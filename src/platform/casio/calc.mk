# fx-CG50 add-in build (included from the top-level Makefile).
SH_PREFIX ?= $(if $(wildcard ../../.toolchain/prefix/bin/sh-elf-gcc),../../.toolchain/prefix/bin/sh-elf-,/opt/sh-elf/bin/sh-elf-)
SH_CC := $(SH_PREFIX)gcc
SH_OBJCOPY := $(SH_PREFIX)objcopy
CALC_DIR := build/calc
CALC_CFLAGS := -m4-nofpu -mb -O2 -ffreestanding -nostdlib -fno-pie -fno-builtin-printf -std=c99 \
	-Wall -Wno-unused-parameter -Isrc/platform/casio/include -ffunction-sections -fdata-sections $(CALC_EXTRA)
CALC_SRC := $(ENGINE) src/platform/casio/main.c src/platform/casio/libc_min.c
CALC_ASM := src/platform/casio/crt0.S src/platform/casio/syscalls.S
CALC_NAME ?= SM50
# Optional native recompilation probe. It builds a single Q12 ground-slide
# update from the local PS1 source port into an alternate add-in, without
# changing the normal SM50 target. Set PS1_PORT to the local reference tree.
PS1_PORT ?= ../psx2
ifeq ($(SM50_NATIVE_PROBE),1)
CALC_PS1_CFLAGS := -m4-nofpu -mb -O2 -ffreestanding -fno-builtin -fno-pic \
	-ffunction-sections -fdata-sections -std=gnu2x -DNON_MATCHING=1 -DAVOID_UB=1 \
	-I$(PS1_PORT)/include -I$(PS1_PORT)/src -I$(PS1_PORT) \
	-I$(PS1_PORT)/include/libc -I$(PS1_PORT)/ps1 -I$(PS1_PORT)/ps1-bare-metal \
	-I$(PS1_PORT)/ps1-bare-metal/libc -DENABLE_RUMBLE=0 -DRUMBLE_GRAPHIC=0
CALC_PS1_OBJECTS := $(CALC_DIR)/native/mario_actions_moving.c.o \
	$(CALC_DIR)/native/math_util.c.o $(CALC_DIR)/native/fract_math.c.o \
	$(CALC_DIR)/native/float_math.c.o \
	$(CALC_DIR)/native/ps1_softfloat_bridge.c.o \
	$(CALC_DIR)/native/native_sm64_movement_probe.c.o
CALC_NATIVE_BRIDGE := $(CALC_DIR)/native/native_sm64_collision_bridge.c.o
endif
# Optional probe against the canonical CC0 decompilation checkout.
CANONICAL_SM64 ?= deps/n64decomp-sm64
ifeq ($(SM50_CANONICAL_PROBE),1)
CALC_CANONICAL_CFLAGS := -m4-nofpu -mb -O2 -ffreestanding -fno-builtin -fno-pic \
	-ffunction-sections -fdata-sections -std=gnu2x -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C -DM_PI=3.14159265358979323846 \
	-Dapproach_s32=sm64_approach_s32 \
	-I$(CANONICAL_SM64)/include -I$(CANONICAL_SM64)/src -I$(CANONICAL_SM64) -I$(CANONICAL_SM64)/pc
CALC_CANONICAL_OBJECTS := $(CALC_DIR)/canonical/mario_actions_moving.c.o \
	$(CALC_DIR)/canonical/math_util.c.o $(CALC_DIR)/canonical/native_sm64_canonical_probe.c.o
CALC_NATIVE_BRIDGE := $(CALC_DIR)/native/native_sm64_collision_bridge.c.o
CALC_NATIVE_LIBS := -lm
endif
# CALC_EMBED=1 links content/sm50.pack into the add-in (test builds); otherwise the
# data pack is appended at install time by tools/build-g3a.py.
CALC_EMBED ?= 0
ifeq ($(CALC_EMBED),1)
CALC_PACK_OBJ := src/platform/casio/pack.S
CALC_CFLAGS += -DSM50_EMBED_PACK
else
CALC_PACK_OBJ :=
endif

calc: $(CALC_DIR)/$(CALC_NAME).g3a

$(CALC_DIR)/$(CALC_NAME).elf: $(CALC_SRC) $(CALC_ASM) $(CALC_PS1_OBJECTS) $(CALC_CANONICAL_OBJECTS) $(CALC_NATIVE_BRIDGE) src/platform/casio/pack.S src/platform/casio/prizm.ld content/sm50.pack src/engine/*.h
	@mkdir -p $(CALC_DIR)
	$(SH_CC) $(CALC_CFLAGS) -T src/platform/casio/prizm.ld -Wl,--gc-sections -Wl,-Map,$(CALC_DIR)/$(CALC_NAME).map \
		-DSM50_PACK_PATH='"content/sm50.pack"' -o $@ $(CALC_ASM) $(CALC_PACK_OBJ) $(CALC_SRC) $(CALC_PS1_OBJECTS) $(CALC_CANONICAL_OBJECTS) $(CALC_NATIVE_BRIDGE) -lgcc $(CALC_NATIVE_LIBS)

$(CALC_DIR)/native/mario_actions_moving.c.o: $(PS1_PORT)/src/game/mario_actions_moving.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -c $< -o $@
$(CALC_DIR)/native/math_util.c.o: $(PS1_PORT)/src/engine/math_util.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -Dapproach_s32=ps1_approach_s32 -c $< -o $@
$(CALC_DIR)/native/fract_math.c.o: $(PS1_PORT)/src/port/fract_math.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -c $< -o $@
$(CALC_DIR)/native/float_math.c.o: $(PS1_PORT)/src/port/float_math.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -Dqtof=ps1_qtof -Dftoq=ps1_ftoq -c $< -o $@
$(CALC_DIR)/native/ps1_softfloat_bridge.c.o: src/platform/casio/ps1_softfloat_bridge.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -c $< -o $@
$(CALC_DIR)/native/native_sm64_movement_probe.c.o: tests/native_sm64_movement_probe.c
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_PS1_CFLAGS) -c $< -o $@
$(CALC_DIR)/native/native_sm64_collision_bridge.c.o: src/platform/casio/native_sm64_collision_bridge.c src/engine/world.h
	@mkdir -p $(CALC_DIR)/native
	$(SH_CC) $(CALC_CFLAGS) -c $< -o $@
$(CALC_DIR)/canonical/mario_actions_moving.c.o: $(CANONICAL_SM64)/src/game/mario_actions_moving.c
	@mkdir -p $(CALC_DIR)/canonical
	$(SH_CC) $(CALC_CANONICAL_CFLAGS) -c $< -o $@
$(CALC_DIR)/canonical/math_util.c.o: $(CANONICAL_SM64)/src/engine/math_util.c
	@mkdir -p $(CALC_DIR)/canonical
	$(SH_CC) $(CALC_CANONICAL_CFLAGS) -c $< -o $@
$(CALC_DIR)/canonical/native_sm64_canonical_probe.c.o: tests/native_sm64_canonical_probe.c
	@mkdir -p $(CALC_DIR)/canonical
	$(SH_CC) $(CALC_CANONICAL_CFLAGS) -c $< -o $@

$(CALC_DIR)/$(CALC_NAME).bin: $(CALC_DIR)/$(CALC_NAME).elf
	$(SH_OBJCOPY) -O binary -R .bss $< $@

$(CALC_DIR)/$(CALC_NAME).g3a: $(CALC_DIR)/$(CALC_NAME).bin tools/mkg3a.py
	python3 tools/mkg3a.py -n "basic:SM50" $(if $(wildcard assets/icon_uns.png),-i uns:assets/icon_uns.png -i sel:assets/icon_sel.png) $< $@
	python3 tools/g3a_check.py $@ >/dev/null
