#!/bin/bash
# SM50 installer for macOS: copies SM50.g3a onto a USB-connected Casio fx-CG50.
# Usage: double-click this file in Finder, or run:  ./install-sm50.command [path/to/SM50.g3a]
# On the calculator: connect the USB cable and choose "USB Flash" ([F1]) when asked.
set -u
cd "$(dirname "$0")"
G3A="${1:-dist/SM50.g3a}"
red() { printf '\033[31m%s\033[0m\n' "$*"; }
grn() { printf '\033[32m%s\033[0m\n' "$*"; }
die() { red "ERROR: $*"; echo; read -r -p "Press Return to close..." _ || true; exit 1; }

echo "=== SM50 installer ==="
[ -f "$G3A" ] || die "add-in not found: $G3A (re-extract the archive, or build it with 'make calc')"

# --- validate the add-in container ---------------------------------------
SIZE=$(wc -c < "$G3A" | tr -d ' ')
MAGIC=$(head -c 8 "$G3A" | od -An -tx1 | tr -d ' \n')
[ "$MAGIC" = "aaacbdaf90889a8d" ] || die "$G3A is not a valid g3a add-in (bad header)"
[ "$SIZE" -gt 30000 ] || die "$G3A is too small ($SIZE bytes)"
if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys' >/dev/null 2>&1; then
    python3 tools/g3a_check.py "$G3A" >/dev/null 2>&1 || die "checksum validation failed for $G3A"
    grn "Add-in OK: $G3A ($SIZE bytes, checksums valid)"
else
    grn "Add-in header OK: $G3A ($SIZE bytes) (python3 not available: skipped checksum test)"
fi
for rom in *.z64 *.n64 *.v64; do
    [ -e "$rom" ] && echo "Note: '$rom' found. SM50 does not use a ROM file; it is left untouched."
done

# --- find the calculator volume -------------------------------------------
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
    for _ in $(seq 1 60); do
        sleep 2; echo -n "."
        VOL=$(find_calc) && break
    done
    echo
fi
if [ -z "$VOL" ]; then
    echo "Could not auto-detect the calculator. Mounted volumes:"
    ls -1 /Volumes
    read -r -p "Type the calculator volume name (or Return to cancel): " NAME
    [ -n "$NAME" ] && [ -d "/Volumes/$NAME" ] && VOL="/Volumes/$NAME"
fi
[ -n "$VOL" ] || die "no calculator volume found. Is the calculator in USB Flash mode?"
grn "Calculator storage: $VOL"

FREE_KB=$(df -k "$VOL" | awk 'NR==2 {print $4}')
NEED_KB=$(( SIZE / 1024 + 64 ))
[ -z "$FREE_KB" ] || [ "$FREE_KB" -ge "$NEED_KB" ] || die "not enough free space on the calculator (${FREE_KB} KB free, ${NEED_KB} KB needed). Delete files in MEMORY > Storage."

# --- copy -------------------------------------------------------------------
cp "$G3A" "$VOL/SM50.g3a" || die "copy failed"
sync
cmp -s "$G3A" "$VOL/SM50.g3a" || die "verification after copy failed (file differs)"
# remove Finder metadata files the calculator does not need
rm -f "$VOL/._SM50.g3a" 2>/dev/null
grn "Copied and verified SM50.g3a."
if command -v diskutil >/dev/null 2>&1; then
    diskutil eject "$VOL" >/dev/null 2>&1 && grn "Calculator ejected safely." || echo "Eject manually before unplugging (the calculator finishes saving when you disconnect)."
fi
echo
echo "On the calculator: wait for the 'Updating Add-In' screen to finish, press [MENU],"
echo "then choose the SM50 icon. Controls: arrows move, SHIFT/EXE jump, ALPHA ground-pound,"
echo "F1/F2 rotate camera, F3 zoom, F4 low-res (faster) mode, F6 FPS counter, EXIT pause, MENU leave."
read -r -p "Press Return to close..." _ || true
