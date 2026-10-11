/* The original SM50 content pack, embedded in the add-in (read-only, in flash). */
    .section .rodata.pack, "a"
    .align 4
    .global _sm50_pack_start
    .global _sm50_pack_end
_sm50_pack_start:
    .incbin SM50_PACK_PATH
_sm50_pack_end:
