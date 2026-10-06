; x87_reject.asm — x64 MASM
; double x87_compute_s(uint32_t r1, uint32_t r2)
; 用 x87 指令计算 s = (2*r1*KINV-1)^2 + (2*r2*KINV-1)^2
; 只返回 s（拒绝判定用它），u/v 在 C++ 里重新算
; 全程 80 位中间精度

.code

x87_compute_s PROC
    ; RCX = r1, RDX = r2 → 返回 XMM0 = s

    sub rsp, 48

    ; r1 → int64
    mov ecx, ecx
    mov qword ptr [rsp], rcx

    ; u = fild * KINV * 2 - 1
    lea rax, [KINV_VAL]
    fild qword ptr [rsp]
    fmul qword ptr [rax]
    fadd st, st
    lea rax, [ONE_VAL]
    fsub qword ptr [rax]
    fstp qword ptr [rsp+8]    ; 存 u

    ; r2 → int64
    mov rax, rdx
    mov qword ptr [rsp], rax

    ; v = fild * KINV * 2 - 1
    lea rax, [KINV_VAL]
    fild qword ptr [rsp]
    fmul qword ptr [rax]
    fadd st, st
    lea rax, [ONE_VAL]
    fsub qword ptr [rax]

    ; s = v*v + u*u
    fld st(0)
    fmulp st(1), st
    fld qword ptr [rsp+8]
    fmul qword ptr [rsp+8]
    faddp st(1), st

    fstp qword ptr [rsp+16]
    movsd xmm0, qword ptr [rsp+16]

    add rsp, 48
    ret

    ALIGN 8
KINV_VAL DQ 02.3283064370807974e-10
ONE_VAL  DQ 01.0

x87_compute_s ENDP

END
