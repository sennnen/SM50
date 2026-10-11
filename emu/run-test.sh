#!/usr/bin/env bash
# Build sm50emu + the test add-in, run it with a key script, take screenshots
# and verify that the square moved as scripted.
#   emu/run-test.sh [OUTDIR]       (default emu/test-addin/build/run)
# Env: CASIO_EMU_WORK (casio-emu checkout, default /home/user/ext/casio-emu-build),
#      SH_PREFIX (default /opt/sh-elf/bin/sh-elf-)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${CASIO_EMU_WORK:-/home/user/ext/casio-emu-build}
OUT=${1:-$HERE/test-addin/build/run}
EMU=$WORK/build-headless/sm50emu

"$HERE/fetch-and-build.sh" "$WORK"
make -s -C "$HERE/test-addin" ${SH_PREFIX:+SH_PREFIX=$SH_PREFIX}
G3A=$HERE/test-addin/build/SM50TEST.g3a
python3 "$HERE/../tools/g3a_check.py" "$G3A" | tail -1

rm -rf "$OUT"; mkdir -p "$OUT/fs"
set -x
"$EMU" --keys "$HERE/test-addin/test-keys.txt" --fs "$OUT/fs" --outdir "$OUT" \
       --shot-at 1,40,65,90,119 --shot-every 50 --shot-final \
       --frame-stats "$OUT/frames.csv" --log "$OUT/sm50emu.log" --max-wall 60 \
       "$G3A" | tee "$OUT/stdout.txt"
set +x
# expected top-left corners: start (180,100); RIGHT x30 frames *3px; DOWN x20 *3;
# SHIFT+LEFT x20 *6; UP x10 *3
python3 "$HERE/test-addin/check_shots.py" "$OUT" 1:180:100 40:270:100 65:270:160 90:150:160 119:150:130
grep -q "file: create=0 handle=256 write=16 size=16 read=16 text=hello, fls0!" "$OUT/stdout.txt"
cmp -s <(printf 'hello, fls0!\0\0\0\0') "$OUT/fs/SM50TEST.dat"
grep -q "GetKey -> 30004" "$OUT/stdout.txt"
grep -q "keymatrix: w3=512 l1=512 b6=2" "$OUT/stdout.txt"
grep -q "add-in returned from main" "$OUT/stdout.txt"
grep -q "unknown syscalls  : 0x1234(x1)" "$OUT/stdout.txt"
grep -q "unknown syscall 0x1234" "$OUT/sm50emu.log"
echo "PASS: screenshots in $OUT, Bfile/heap/GetKey/debug-print checks OK"

# crash path: F1 makes the add-in dereference an unmapped address
set +e
"$EMU" --keys "$HERE/test-addin/crash-keys.txt" "$G3A" > "$OUT/crash.txt" 2>&1
rc=$?
set -e
grep -q "EXPEVT=040 TEA=12345678" "$OUT/crash.txt" && [ $rc -eq 3 ] && echo "PASS: crash reported (exit code 3)"
