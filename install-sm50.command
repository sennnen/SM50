#!/bin/bash
# SM50 installer for macOS.
#   1. validates the ROM in this folder (any byte order)
#   2. converts it with the manifest (rom-manifest.json) into an SM50 data pack
#   3. attaches the pack to the prebuilt engine (dist/SM50-engine.bin) -> SM50.g3a
#   4. copies SM50.g3a to a USB-connected fx-CG50 and verifies the copy
# Usage: ./install-sm50.command [rom-file] [manifest.json]
set -u
cd "$(dirname "$0")"
red() { printf '\033[31m%s\033[0m\n' "$*"; }
grn() { printf '\033[32m%s\033[0m\n' "$*"; }
die() { red "ERROR: $*"; echo; read -r -p "Press Return to close..." _ || true; exit 1; }
echo "=== SM50 installer ==="
command -v python3 >/dev/null 2>&1 && python3 -c 'import sys; sys.exit(sys.version_info < (3, 8))' \
    || die "python3 (3.8+) is required. Install the Xcode command line tools: xcode-select --install"
[ -f dist/SM50-engine.bin ] || die "dist/SM50-engine.bin missing (re-extract the archive or run 'make calc')"

ROM="${1:-}"
if [ -z "$ROM" ]; then
    for f in *.z64 *.Z64 *.n64 *.N64 *.v64 *.V64; do [ -f "$f" ] && { ROM="$f"; break; }; done
fi
[ -n "$ROM" ] || die "no ROM found. Put your .z64/.n64/.v64 file in this folder (or pass it as the first argument)."
grn "ROM: $ROM"
python3 tools/n64info.py "$ROM" || die "ROM validation failed"

MANIFEST="${2:-rom-manifest.json}"
[ -f "$MANIFEST" ] || die "no conversion manifest ($MANIFEST). SM50 converts a ROM by following a manifest that
       lists the ROM ranges, segments and display lists to load (format: tools/n64pack.py --help).
       No manifest for this ROM is included; see MORNING-STATUS.md."
mkdir -p build
python3 tools/n64pack.py "$MANIFEST" --rom "$ROM" -o build/rom.pack || die "conversion failed"
python3 tools/build-g3a.py dist/SM50-engine.bin build/rom.pack build/SM50.g3a || die "add-in build failed"
python3 tools/g3a_check.py build/SM50.g3a >/dev/null || die "built add-in failed validation"
G3A=build/SM50.g3a
SIZE=$(wc -c < "$G3A" | tr -d ' ')
grn "Built $G3A ($SIZE bytes)"

find_calc() {
    for v in /Volumes/*; do
        [ -d "$v" ] || continue
        n=$(basename "$v")
        case "$n" in Macintosh*|Recovery*|Preboot*|VM|Data|com.apple*) continue;; esac
        if [ -d "$v/@MainMem" ] || ls "$v"/*.g3a >/dev/null 2>&1 || echo "$n" | grep -qi casio; then
            echo "$v"; return 0
        fi
    done
    return 1
}
VOL=$(find_calc)
if [ -z "$VOL" ]; then
    echo "Connect the fx-CG50 with its USB cable and press [F1] (USB Flash) on the calculator."
    echo -n "Waiting for the calculator to mount (up to 2 minutes)"
    for _ in $(seq 1 60); do sleep 2; echo -n "."; VOL=$(find_calc) && break; done
    echo
fi
if [ -z "$VOL" ]; then
    echo "Could not auto-detect the calculator. Mounted volumes:"; ls -1 /Volumes
    read -r -p "Type the calculator volume name (or Return to cancel): " NAME
    [ -n "$NAME" ] && [ -d "/Volumes/$NAME" ] && VOL="/Volumes/$NAME"
fi
[ -n "$VOL" ] || die "no calculator volume found. Is the calculator in USB Flash mode?"
grn "Calculator storage: $VOL"
FREE_KB=$(df -k "$VOL" | awk 'NR==2 {print $4}')
NEED_KB=$(( SIZE / 1024 + 64 ))
[ -z "$FREE_KB" ] || [ "$FREE_KB" -ge "$NEED_KB" ] || die "not enough free space (${FREE_KB} KB free, ${NEED_KB} KB needed)"
cp "$G3A" "$VOL/SM50.g3a" || die "copy failed"
sync
cmp -s "$G3A" "$VOL/SM50.g3a" || die "verification after copy failed"
rm -f "$VOL/._SM50.g3a" 2>/dev/null
grn "Copied and verified SM50.g3a."
command -v diskutil >/dev/null 2>&1 && { diskutil eject "$VOL" >/dev/null 2>&1 && grn "Calculator ejected." || echo "Eject manually before unplugging."; }
echo "On the calculator: MENU -> SM50. Arrows move, SHIFT jump, F1/F2 camera, F3 zoom, F4 speed mode, F6 FPS, EXIT pause."
read -r -p "Press Return to close..." _ || true
