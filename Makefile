# SM50 build. Targets:
#   make host     - native headless test runner (build/host/sm50-host)
#   make calc     - fx-CG50 add-in (build/calc/SM50.g3a), needs sh-elf toolchain
#   make content  - original content pack (content/sm50.pack)
#   make test     - run automated tests
ENGINE := src/engine/fixmath.c src/engine/tables.c src/engine/gfx.c src/engine/world.c src/engine/player.c src/engine/game.c
HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c99 -D_DEFAULT_SOURCE

all: content host

content: content/sm50.pack
content/sm50.pack: tools/content/build_content.py tools/content/gbi.py
	python3 tools/content/build_content.py $@

src/engine/tables.c: tools/gen_tables.py
	python3 tools/gen_tables.py $@

src/engine/font.h: tools/gen_font.py
	python3 tools/gen_font.py

host: build/host/sm50-host
build/host/sm50-host: $(ENGINE) src/platform/host/main.c src/engine/*.h
	@mkdir -p build/host
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(ENGINE) src/platform/host/main.c

include src/platform/casio/calc.mk

calc-native-probe:
	$(MAKE) calc CALC_NAME=SM64NativeProbe SM50_NATIVE_PROBE=1 CALC_EXTRA='-DSM50_NATIVE_PROBE -DSM50_EMU_DEBUG'

calc-canonical-probe:
	$(MAKE) calc CALC_NAME=SM64CanonicalProbe SM50_CANONICAL_PROBE=1 CALC_EXTRA='-DSM50_CANONICAL_PROBE -DSM50_EMU_DEBUG'

test: host content
	python3 tests/run_tests.py

test-rom: host
	python3 tests/test_sm64_rom.py "$$SM64_ROM"

clean:
	rm -rf build

.PHONY: all content host calc calc-native-probe calc-canonical-probe test test-rom clean
