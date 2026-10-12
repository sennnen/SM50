/* OS syscall stubs (numbers from libfxcg, BSD licence). r0 = syscall number. */
#define SC(name, num) \
    .text; .align 2; .global name; name: \
    mov.l 1f, r2; mov.l 2f, r0; jmp @r2; nop; .align 2; 1: .long 0x80020070; 2: .long num

SC(_GetVRAMAddress, 0x01E6)
SC(_Bdisp_PutDisp_DD, 0x025F)
SC(_Bdisp_AllClr_VRAM, 0x0272)
SC(_Bdisp_EnableColor, 0x0921)
SC(_EnableStatusArea, 0x02B7)
SC(_RTC_GetTicks, 0x02C1)
SC(_GetKey, 0x0EAB)
SC(_PrintXY, 0x18F9)
SC(_sys_malloc, 0x1F44)
SC(_sys_free, 0x1F42)
SC(_emu_debug_print, 0x7FFF)
