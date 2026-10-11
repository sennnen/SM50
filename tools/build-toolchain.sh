#!/usr/bin/env bash
# Builds a bare-metal big-endian SuperH (sh-elf) GCC toolchain for the fx-CG50.
# Usage: build-toolchain.sh <src-dir-with-tarballs> <prefix>
set -euo pipefail
SRC=${1:?src dir}; PREFIX=${2:?prefix}; J=${J:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}
cd "$SRC"
[ -d binutils-2.45 ] || tar xf binutils_2.45.orig.tar.xz
if [ ! -d gcc-14.3.0 ]; then tar xzf gcc-14_14.3.0.orig.tar.gz; tar xf gcc-14-14.3.0/gcc-14.3.0.tar.xz; fi
export PATH="$PREFIX/bin:$PATH"
mkdir -p build-binutils && cd build-binutils
../binutils-2.45/configure --target=sh-elf --prefix="$PREFIX" --disable-nls --disable-werror --disable-gdb --disable-sim
make -j"$J" && make install
cd .. && mkdir -p build-gcc && cd build-gcc
../gcc-14.3.0/configure --target=sh-elf --prefix="$PREFIX" --enable-languages=c \
  --with-multilib-list=m4-nofpu --with-endian=big --without-headers --with-newlib \
  --disable-nls --disable-shared --disable-threads --disable-libssp --disable-libquadmath \
  --disable-libgomp --disable-libatomic --enable-lto
make -j"$J" all-gcc all-target-libgcc && make install-gcc install-target-libgcc
echo TOOLCHAIN_OK
