; x87_test.asm — 验证 x87 80位 vs SSE2 64位在 a*b+c 上的差异
; double x87_mul_add(double a, double b, double c)
; 返回 a * b + c（x87 80位中间精度）

.code

x87_mul_add PROC
    ; RCX = a (as double in XMM0), RDX = b (in XMM1), R8 = c (in XMM2)
    ; Wait, Win64 ABI for doubles: XMM0, XMM1, XMM2

    sub rsp, 32

    ; 存 a, b, c 到栈
    movsd qword ptr [rsp], xmm0      ; a
    movsd qword ptr [rsp+8], xmm1    ; b
    movsd qword ptr [rsp+16], xmm2   ; c

    ; x87 计算: a * b + c
    fld qword ptr [rsp]               ; st0 = a (80位)
    fmul qword ptr [rsp+8]            ; st0 = a*b (80位)
    fadd qword ptr [rsp+16]           ; st0 = a*b+c (80位)
    fstp qword ptr [rsp+24]           ; 存结果 (截断到 64位)

    movsd xmm0, qword ptr [rsp+24]   ; 返回 XMM0

    add rsp, 32
    ret

x87_mul_add ENDP

END
