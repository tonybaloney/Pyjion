.intel_syntax noprefix
#define PAGE_SIZE 0x1000
.global JIT_StackProbe
.type JIT_StackProbe, %function
JIT_StackProbe:
        .cfi_startproc
        // On entry:
        //   r11 - points to the lowest address on the stack frame being allocated (i.e. [InitialSp - FrameSize])
        //   rsp - points to some byte on the last probed page
        // On exit:
        //   r11 - is preserved
        //
        // NOTE: this helper will probe at least one page below the one pointed by rsp.
        push rbp
        .cfi_adjust_cfa_offset 8
        .cfi_rel_offset rbp, 0
        mov     rbp, rsp
        .cfi_def_cfa_register rbp
        .cfi_def_cfa_offset 16
        and     rsp, -PAGE_SIZE        // rsp points to the **lowest address** on the last probed page
                                       // This is done to make the following loop end condition simpler.

ProbeLoop:
        sub     rsp, PAGE_SIZE         // rsp points to the lowest address of the **next page** to probe
        test    dword ptr [rsp], eax   // rsp points to the lowest address on the **last probed** page
        cmp     rsp, r11
        jg      ProbeLoop // if (rsp > r11), then we need to probe at least one more page.

        mov     rsp, rbp
        .cfi_def_cfa_register rsp
        .cfi_def_cfa_offset 16
        pop rbp
        .cfi_adjust_cfa_offset -8
        .cfi_restore rbp
        .cfi_same_value rbp
        ret
    .cfi_endproc