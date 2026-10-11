/* SM50 test add-in start-up code (original; follows the classic Prizm
 * add-in start-up design: copy .data from ROM to RAM, clear .bss, call the
 * OS execution check, then tail-call main(isAppli, optionNum)). */
    .section .pretext, "ax"
    .global _start
    .align 2
_start:
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    mov.l   r4, @-r15
    mov     r5, r14

    /* .data: copy [ldata, ldata + (edata - sdata)) -> [sdata, edata) */
    mov.l   .L_ldata, r0
    mov.l   .L_sdata, r1
    mov.l   .L_edata, r2
1:  cmp/hs  r2, r1
    bt      2f
    mov.l   @r0+, r3
    mov.l   r3, @r1
    bra     1b
    add     #4, r1
2:
    /* .bss: zero [sbss, ebss) */
    mov.l   .L_sbss, r1
    mov.l   .L_ebss, r2
    mov     #0, r3
3:  cmp/hs  r2, r1
    bt      4f
    mov.l   r3, @r1
    bra     3b
    add     #4, r1
4:
    /* GlibAddinAplExecutionCheck(0, 1, 1) */
    mov     #0, r4
    mov     #1, r5
    mov     #1, r6
    mov.l   .L_syscall, r2
    mov     #0x29, r0
    jsr     @r2
    nop

    mov.l   @r15+, r4
    extu.w  r14, r5
    lds.l   @r15+, pr
    mov.l   .L_main, r0
    jmp     @r0
    mov.l   @r15+, r14

    .align 2
.L_ldata:   .long _ldata
.L_sdata:   .long _sdata
.L_edata:   .long _edata
.L_sbss:    .long _sbss
.L_ebss:    .long _ebss
.L_syscall: .long 0x80020070
.L_main:    .long _main
