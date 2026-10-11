/* SM50 fx-CG50 add-in entry: copy .data, clear .bss, call main(). */
    .section ".pretext", "ax"
    .global initialize
    .align 2
initialize:
    mov.l r14, @-r15
    sts.l pr, @-r15
    mov.l r4, @-r15
    mov r5, r14
    mov.l L_datald, r0
    mov.l L_sdata, r2
    mov.l L_edata, r3
1:  cmp/hs r3, r2
    bt 2f
    mov.l @r0+, r1
    mov.l r1, @r2
    bra 1b
    add #4, r2
2:  mov.l L_bbss, r2
    mov.l L_ebss, r3
    mov #0, r1
3:  cmp/hs r3, r2
    bt 4f
    mov.l r1, @r2
    bra 3b
    add #4, r2
4:  mov #1, r4
    mov #1, r5
    mov.l L_syscall, r2
    jsr @r2
    mov #0x29, r0           /* GlibAddinAplExecutionCheck(0, 1, 1) */
    mov.l @r15, r4
    mov r14, r5
    mov.l L_main, r0
    jsr @r0
    nop
    add #4, r15
    lds.l @r15+, pr
    rts
    mov.l @r15+, r14
    .align 4
L_main:    .long _main
L_syscall: .long 0x80020070
L_datald:  .long _datald
L_sdata:   .long _sdata
L_edata:   .long _edata
L_bbss:    .long _bbss
L_ebss:    .long _ebss
