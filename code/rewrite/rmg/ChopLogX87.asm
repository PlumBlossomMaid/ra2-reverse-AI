; ChopLogX87.asm — x64 MASM 实现 of x87 fyl2x * ln2
; 原版 0x598126: fldln2 / fxch / fyl2x
; x64 不支持 __asm 内联，用外部 .asm 文件

.code
ChopLogX87 proc
    ; xmm0 = s (double, 参数)
    ; 用 x87 指令（x64 仍支持 x87）
    fldln2          ; st0 = ln2
    movsd QWORD PTR [rsp+8], xmm0
    fld QWORD PTR [rsp+8]  ; st0 = s, st1 = ln2
    fyl2x           ; st0 = ln2 * log2(s) = ln(s)
    fstp QWORD PTR [rsp+8]
    movsd xmm0, QWORD PTR [rsp+8]
    ret
ChopLogX87 endp
end
