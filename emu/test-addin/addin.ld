/* SM50 test add-in linker script (original; same memory model as the
 * classic Prizm SDK script: code mapped at 0x00300000 right after the
 * 0x7000-byte g3a header, initialised data/bss in the static RAM window
 * at 0x08100000). */
OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
    rom (rx)  : o = 0x00300000, l = 2048k
    ram (rwx) : o = 0x08100000, l = 512k
}

SECTIONS
{
    .text : {
        *(.pretext)
        *(.text .text.*)
    } > rom

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
    } > rom

    .data : ALIGN(4) {
        _ldata = LOADADDR(.data);
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > ram AT> rom

    .bss (NOLOAD) : ALIGN(4) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > ram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
