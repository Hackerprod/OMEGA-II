; Assembly-only P_instr loops. The QPC wall interval is measured by the caller;
; RDTSCP here brackets only the register arithmetic loop. RCX=outer iterations, RDX=stamp*, R8=lane sink*.
CONST SEGMENT READONLY 'DATA'
ALIGN 16
phase1_instr_activations WORD 127,-127,96,-96,64,-64,32,-32,16,-16,8,-8,4,-4,2,-2
phase1_instr_weights WORD 7,-7,7,-7,7,-7,7,-7,7,-7,7,-7,7,-7,7,-7
ALIGN 16
phase1_instr_acc0 DWORD 0,0,0,0,0,0,0,0
phase1_instr_acc1 DWORD 1,1,1,1,1,1,1,1
phase1_instr_acc2 DWORD 2,2,2,2,2,2,2,2
phase1_instr_acc3 DWORD 3,3,3,3,3,3,3,3
phase1_instr_acc4 DWORD 4,4,4,4,4,4,4,4
phase1_instr_acc5 DWORD 5,5,5,5,5,5,5,5
phase1_instr_acc6 DWORD 6,6,6,6,6,6,6,6
phase1_instr_acc7 DWORD 7,7,7,7,7,7,7,7
CONST ENDS

AVX2_BATCH MACRO
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm2,ymm2,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm3,ymm3,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm4,ymm4,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm5,ymm5,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm6,ymm6,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm7,ymm7,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm8,ymm8,ymm10
    vpmaddwd ymm10,ymm0,ymm1
    vpaddd ymm9,ymm9,ymm10
ENDM

VNNI_ACC MACRO prefix,modrm
    db 0c4h,prefix,07dh,052h,modrm
ENDM

VNNI_BATCH MACRO
    VNNI_ACC 0e2h,0d1h
    VNNI_ACC 0e2h,0d9h
    VNNI_ACC 0e2h,0e1h
    VNNI_ACC 0e2h,0e9h
    VNNI_ACC 0e2h,0f1h
    VNNI_ACC 0e2h,0f9h
    VNNI_ACC 062h,0c1h
    VNNI_ACC 062h,0c9h
ENDM

.code
PUBLIC phase1_instr_avx2_qa
phase1_instr_avx2_qa PROC
    sub rsp,88
    vmovdqu XMMWORD PTR [rsp+0],xmm6
    vmovdqu XMMWORD PTR [rsp+16],xmm7
    vmovdqu XMMWORD PTR [rsp+32],xmm8
    vmovdqu XMMWORD PTR [rsp+48],xmm9
    vmovdqu XMMWORD PTR [rsp+64],xmm10
    vmovdqu ymm0,YMMWORD PTR phase1_instr_activations
    vmovdqu ymm1,YMMWORD PTR phase1_instr_weights
    vmovdqu ymm2,YMMWORD PTR phase1_instr_acc0
    vmovdqu ymm3,YMMWORD PTR phase1_instr_acc1
    vmovdqu ymm4,YMMWORD PTR phase1_instr_acc2
    vmovdqu ymm5,YMMWORD PTR phase1_instr_acc3
    vmovdqu ymm6,YMMWORD PTR phase1_instr_acc4
    vmovdqu ymm7,YMMWORD PTR phase1_instr_acc5
    vmovdqu ymm8,YMMWORD PTR phase1_instr_acc6
    vmovdqu ymm9,YMMWORD PTR phase1_instr_acc7
    AVX2_BATCH
    xor eax,eax
    vmovd ecx,xmm2
    add rax,rcx
    vmovd ecx,xmm3
    add rax,rcx
    vmovd ecx,xmm4
    add rax,rcx
    vmovd ecx,xmm5
    add rax,rcx
    vmovd ecx,xmm6
    add rax,rcx
    vmovd ecx,xmm7
    add rax,rcx
    vmovd ecx,xmm8
    add rax,rcx
    vmovd ecx,xmm9
    add rax,rcx
    vzeroupper
    vmovdqu xmm6,XMMWORD PTR [rsp+0]
    vmovdqu xmm7,XMMWORD PTR [rsp+16]
    vmovdqu xmm8,XMMWORD PTR [rsp+32]
    vmovdqu xmm9,XMMWORD PTR [rsp+48]
    vmovdqu xmm10,XMMWORD PTR [rsp+64]
    add rsp,88
    ret
phase1_instr_avx2_qa ENDP

PUBLIC phase1_instr_vnni_qa
phase1_instr_vnni_qa PROC
    sub rsp,88
    vmovdqu XMMWORD PTR [rsp+0],xmm6
    vmovdqu XMMWORD PTR [rsp+16],xmm7
    vmovdqu XMMWORD PTR [rsp+32],xmm8
    vmovdqu XMMWORD PTR [rsp+48],xmm9
    vmovdqu XMMWORD PTR [rsp+64],xmm10
    vmovdqu ymm0,YMMWORD PTR phase1_instr_activations
    vmovdqu ymm1,YMMWORD PTR phase1_instr_weights
    vmovdqu ymm2,YMMWORD PTR phase1_instr_acc0
    vmovdqu ymm3,YMMWORD PTR phase1_instr_acc1
    vmovdqu ymm4,YMMWORD PTR phase1_instr_acc2
    vmovdqu ymm5,YMMWORD PTR phase1_instr_acc3
    vmovdqu ymm6,YMMWORD PTR phase1_instr_acc4
    vmovdqu ymm7,YMMWORD PTR phase1_instr_acc5
    vmovdqu ymm8,YMMWORD PTR phase1_instr_acc6
    vmovdqu ymm9,YMMWORD PTR phase1_instr_acc7
    VNNI_BATCH
    xor eax,eax
    vmovd ecx,xmm2
    add rax,rcx
    vmovd ecx,xmm3
    add rax,rcx
    vmovd ecx,xmm4
    add rax,rcx
    vmovd ecx,xmm5
    add rax,rcx
    vmovd ecx,xmm6
    add rax,rcx
    vmovd ecx,xmm7
    add rax,rcx
    vmovd ecx,xmm8
    add rax,rcx
    vmovd ecx,xmm9
    add rax,rcx
    vzeroupper
    vmovdqu xmm6,XMMWORD PTR [rsp+0]
    vmovdqu xmm7,XMMWORD PTR [rsp+16]
    vmovdqu xmm8,XMMWORD PTR [rsp+32]
    vmovdqu xmm9,XMMWORD PTR [rsp+48]
    vmovdqu xmm10,XMMWORD PTR [rsp+64]
    add rsp,88
    ret
phase1_instr_vnni_qa ENDP

PUBLIC phase1_instr_avx2
phase1_instr_avx2 PROC
    mov r9,rdx
    mov r11,rcx
    sub rsp,88
    vmovdqu XMMWORD PTR [rsp+0],xmm6
    vmovdqu XMMWORD PTR [rsp+16],xmm7
    vmovdqu XMMWORD PTR [rsp+32],xmm8
    vmovdqu XMMWORD PTR [rsp+48],xmm9
    vmovdqu XMMWORD PTR [rsp+64],xmm10
    vmovdqu ymm0,YMMWORD PTR phase1_instr_activations
    vmovdqu ymm1,YMMWORD PTR phase1_instr_weights
    vmovdqu ymm2,YMMWORD PTR phase1_instr_acc0
    vmovdqu ymm3,YMMWORD PTR phase1_instr_acc1
    vmovdqu ymm4,YMMWORD PTR phase1_instr_acc2
    vmovdqu ymm5,YMMWORD PTR phase1_instr_acc3
    vmovdqu ymm6,YMMWORD PTR phase1_instr_acc4
    vmovdqu ymm7,YMMWORD PTR phase1_instr_acc5
    vmovdqu ymm8,YMMWORD PTR phase1_instr_acc6
    vmovdqu ymm9,YMMWORD PTR phase1_instr_acc7
    lfence
    rdtscp
    mov DWORD PTR [r9+8],ecx
    mov r10,rax
    shl rdx,32
    or r10,rdx
    lfence
    test r11,r11
    jz avx2_done
avx2_loop:
    AVX2_BATCH
    AVX2_BATCH
    AVX2_BATCH
    AVX2_BATCH
    dec r11
    jnz avx2_loop
avx2_done:
    lfence
    rdtscp
    mov DWORD PTR [r9+12],ecx
    shl rdx,32
    or rax,rdx
    sub rax,r10
    mov QWORD PTR [r9],rax
    vmovdqu YMMWORD PTR [r8+0],ymm2
    vmovdqu YMMWORD PTR [r8+32],ymm3
    vmovdqu YMMWORD PTR [r8+64],ymm4
    vmovdqu YMMWORD PTR [r8+96],ymm5
    vmovdqu YMMWORD PTR [r8+128],ymm6
    vmovdqu YMMWORD PTR [r8+160],ymm7
    vmovdqu YMMWORD PTR [r8+192],ymm8
    vmovdqu YMMWORD PTR [r8+224],ymm9
    vzeroupper
    vmovdqu xmm6,XMMWORD PTR [rsp+0]
    vmovdqu xmm7,XMMWORD PTR [rsp+16]
    vmovdqu xmm8,XMMWORD PTR [rsp+32]
    vmovdqu xmm9,XMMWORD PTR [rsp+48]
    vmovdqu xmm10,XMMWORD PTR [rsp+64]
    add rsp,88
    ret
phase1_instr_avx2 ENDP

PUBLIC phase1_instr_vnni
phase1_instr_vnni PROC
    mov r9,rdx
    mov r11,rcx
    sub rsp,88
    vmovdqu XMMWORD PTR [rsp+0],xmm6
    vmovdqu XMMWORD PTR [rsp+16],xmm7
    vmovdqu XMMWORD PTR [rsp+32],xmm8
    vmovdqu XMMWORD PTR [rsp+48],xmm9
    vmovdqu XMMWORD PTR [rsp+64],xmm10
    vmovdqu ymm0,YMMWORD PTR phase1_instr_activations
    vmovdqu ymm1,YMMWORD PTR phase1_instr_weights
    vmovdqu ymm2,YMMWORD PTR phase1_instr_acc0
    vmovdqu ymm3,YMMWORD PTR phase1_instr_acc1
    vmovdqu ymm4,YMMWORD PTR phase1_instr_acc2
    vmovdqu ymm5,YMMWORD PTR phase1_instr_acc3
    vmovdqu ymm6,YMMWORD PTR phase1_instr_acc4
    vmovdqu ymm7,YMMWORD PTR phase1_instr_acc5
    vmovdqu ymm8,YMMWORD PTR phase1_instr_acc6
    vmovdqu ymm9,YMMWORD PTR phase1_instr_acc7
    lfence
    rdtscp
    mov DWORD PTR [r9+8],ecx
    mov r10,rax
    shl rdx,32
    or r10,rdx
    lfence
    test r11,r11
    jz vnni_done
vnni_loop:
    VNNI_BATCH
    VNNI_BATCH
    VNNI_BATCH
    VNNI_BATCH
    dec r11
    jnz vnni_loop
vnni_done:
    lfence
    rdtscp
    mov DWORD PTR [r9+12],ecx
    shl rdx,32
    or rax,rdx
    sub rax,r10
    mov QWORD PTR [r9],rax
    vmovdqu YMMWORD PTR [r8+0],ymm2
    vmovdqu YMMWORD PTR [r8+32],ymm3
    vmovdqu YMMWORD PTR [r8+64],ymm4
    vmovdqu YMMWORD PTR [r8+96],ymm5
    vmovdqu YMMWORD PTR [r8+128],ymm6
    vmovdqu YMMWORD PTR [r8+160],ymm7
    vmovdqu YMMWORD PTR [r8+192],ymm8
    vmovdqu YMMWORD PTR [r8+224],ymm9
    vzeroupper
    vmovdqu xmm6,XMMWORD PTR [rsp+0]
    vmovdqu xmm7,XMMWORD PTR [rsp+16]
    vmovdqu xmm8,XMMWORD PTR [rsp+32]
    vmovdqu xmm9,XMMWORD PTR [rsp+48]
    vmovdqu xmm10,XMMWORD PTR [rsp+64]
    add rsp,88
    xor eax,eax
    ret
phase1_instr_vnni ENDP
END
