# Headless SM50 build of casio-emu (no Qt/SDL needed).
#   make -f emu/Makefile CASIO_EMU_DIR=/path/to/patched/casio-emu OUT=/path/to/sm50emu
# Normally driven by emu/fetch-and-build.sh.

CASIO_EMU_DIR ?= /home/user/ext/casio-emu-build
SM50_EMU_DIR  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
OUT           ?= $(CASIO_EMU_DIR)/build-headless/sm50emu
OBJDIR        ?= $(dir $(OUT))obj

CC      ?= cc
OPT     ?= -O2 -flto -g
CFLAGS  ?=
ALL_CFLAGS := $(OPT) $(CFLAGS) -std=gnu11 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
           -Wno-sign-compare -Wno-unused-function -I$(CASIO_EMU_DIR)/src -I$(SM50_EMU_DIR)/src
LDLIBS  += -lm

CORE := interpreter.c instructions.c interrupts.c \
  hardware/cpuRegisters/cpuRegisters.c hardware/display/display.c hardware/dma/dma.c \
  hardware/intc/intc.c hardware/keyboard/keyboard.c hardware/power/power.c \
  hardware/timers/timers.c hardware/tlb/tlb.c hardware/rtc/rtc.c hardware/ports/ports.c \
  hardware/adc/adc.c hardware/bcd/bcd.c hardware/debug/debug.c \
  memory/memory.c memory/specialAddrs.c
OURS := sm50_main.c sm50_hle.c sm50_keys.c sm50_png.c

CORE_OBJS := $(addprefix $(OBJDIR)/core/,$(CORE:.c=.o))
OUR_OBJS  := $(addprefix $(OBJDIR)/sm50/,$(OURS:.c=.o))

all: $(OUT)

$(OUT): $(CORE_OBJS) $(OUR_OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/core/%.o: $(CASIO_EMU_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -w -c -o $@ $<

$(OBJDIR)/sm50/%.o: $(SM50_EMU_DIR)/src/%.c $(SM50_EMU_DIR)/src/sm50.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(OUT)

.PHONY: all clean
