# SM50

An original fixed-point 3D platformer engine for the Casio fx-CG50 (SH4, no FPU), with an N64-style Fast3D display-list renderer, collision, a third-person character controller, an original character and level, a headless emulator test harness and a macOS installer. See `MORNING-STATUS.md` for status and scope.

## Install (macOS)
    ./install-sm50.command

## Build from source (Linux or macOS)
    tools/build-toolchain.sh <dir-with-binutils_2.45.orig.tar.xz-and-gcc-14_14.3.0.orig.tar.gz> /opt/sh-elf
    make content host calc      # content pack, host test runner, build/calc/SM50.g3a
    emu/fetch-and-build.sh      # headless emulator (sm50emu)
    make test

Source tarballs: https://archive.ubuntu.com/ubuntu/pool/main/b/binutils/ and .../pool/main/g/gcc-14/.

## Licences
SM50 code and content are original. libfxcg (BSD) was used as a reference for syscall numbers. casio-emu (Heath123) is fetched at build time and patched; it is not redistributed here.
