/* OS syscall stubs (libfxcg calling convention: r0 = number, jump to
 * 0x80020070, the OS returns to PR).  Numbers as in libfxcg. */
#define SYSCALL(name, num) \
    .text; .align 2; .global name; name: \
    mov.l 1f, r2; mov.l 2f, r0; jmp @r2; nop; \
    .align 2; 1: .long 0x80020070; 2: .long num

SYSCALL(_GetVRAMAddress,        0x01E6)
SYSCALL(_Bdisp_PutDisp_DD,      0x025F)
SYSCALL(_Bdisp_AllClr_VRAM,     0x0272)
SYSCALL(_Bdisp_EnableColor,     0x0921)
SYSCALL(_EnableStatusArea,      0x02B7)
SYSCALL(_RTC_GetTicks,          0x02C1)
SYSCALL(_GetKey,                0x0EAB)
SYSCALL(_sys_malloc,            0x1F44)
SYSCALL(_sys_free,              0x1F42)
SYSCALL(_sys_realloc,           0x1F46)
SYSCALL(_Bfile_OpenFile_OS,     0x1DA3)
SYSCALL(_Bfile_CloseFile_OS,    0x1DA4)
SYSCALL(_Bfile_ReadFile_OS,     0x1DAC)
SYSCALL(_Bfile_WriteFile_OS,    0x1DAF)
SYSCALL(_Bfile_GetFileSize_OS,  0x1DA6)
SYSCALL(_Bfile_CreateEntry_OS,  0x1DAE)
SYSCALL(_Bfile_DeleteEntry,     0x1DB4)
SYSCALL(_Bfile_SeekFile_OS,     0x1DA9)
SYSCALL(_Bfile_StrToName_ncpy,  0x1DDC)
/* SM50 emulator-only debug print: r4 = NUL-terminated string.  Only call it
 * in emulator builds; the real OS has no syscall 0x7FFF. */
SYSCALL(_sm50_debug_print,      0x7FFF)
/* deliberately unimplemented in the emulator: tests the unknown-syscall log */
SYSCALL(_sm50_unknown_probe,    0x1234)
