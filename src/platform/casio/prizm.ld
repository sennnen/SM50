/* SM50 linker script for fx-CG50 add-ins (code mapped at 0x00300000 by the OS).
 * Static RAM: 0x08100000 shares 512 KiB with the stack (which grows down from
 * 0x08180000), so .data+.bss are capped at 320 KiB leaving 192 KiB of stack. */
OUTPUT_ARCH(sh4)
ENTRY(initialize)
MEMORY {
    rom (rx)  : o = 0x00300000, l = 2048k
    ram (rwx) : o = 0x08100000, l = 320k
}
SECTIONS {
    .text : { *(.pretext) *(.text) *(.text.*) } > rom
    .rodata : ALIGN(4) { *(.rodata) *(.rodata.*) . = ALIGN(4); } > rom
    .data : ALIGN(4) {
        _datald = LOADADDR(.data);
        _sdata = .;
        *(.data) *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > ram AT > rom
    _rom_image_end = LOADADDR(.data) + SIZEOF(.data);
    .bss (NOLOAD) : ALIGN(4) {
        _bbss = .;
        *(.bss) *(.bss.*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > ram
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.debug*) }
}
