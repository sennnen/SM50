#!/usr/bin/env bash
# Fetch casio-emu (Heath123) at the pinned commit, apply the SM50 hook patch
# and build the headless emulator binary `sm50emu`.
#
#   emu/fetch-and-build.sh [WORKDIR]      (default /home/user/ext/casio-emu-build)
#
# casio-emu has no explicit license, so its sources are never copied into this
# repository: they are fetched here at build time.  Our own code lives in
# emu/src/ and is compiled against the fetched tree (see emu/Makefile).
# Output: $WORKDIR/build-headless/sm50emu  (also printed at the end)
# Env: CASIO_EMU_URL (default GitHub), CC, OPT (default -O2 -flto -g), CFLAGS (extra), JOBS.
set -euo pipefail

PIN=5d34e298b94be65f7ab0505d3c0f9b9ed802ac4b
URL=${CASIO_EMU_URL:-https://github.com/Heath123/casio-emu.git}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=${1:-/home/user/ext/casio-emu-build}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

if [ -e "$WORK/.sm50-patched" ] && [ "$(cat "$WORK/.sm50-patched")" = "$PIN" ]; then
  echo "== $WORK already fetched + patched at $PIN"
else
  if [ -e "$WORK" ]; then
    echo "error: $WORK exists but is not an SM50-patched casio-emu checkout; remove it or pass another dir" >&2
    exit 1
  fi
  echo "== fetching casio-emu $PIN into $WORK"
  if command -v git >/dev/null 2>&1; then
    git clone --quiet "$URL" "$WORK"
    git -C "$WORK" -c advice.detachedHead=false checkout --quiet "$PIN"
  else
    mkdir -p "$WORK"
    curl -fsSL "https://github.com/Heath123/casio-emu/archive/$PIN.tar.gz" | tar xz -C "$WORK" --strip-components=1
  fi
  echo "== applying emu/casio-emu.patch"
  patch -d "$WORK" -p1 --forward --quiet < "$HERE/casio-emu.patch"
  echo "$PIN" > "$WORK/.sm50-patched"
fi

echo "== building sm50emu"
make -s -f "$HERE/Makefile" -j"$JOBS" CASIO_EMU_DIR="$WORK" OUT="$WORK/build-headless/sm50emu"
echo "== built: $WORK/build-headless/sm50emu"
