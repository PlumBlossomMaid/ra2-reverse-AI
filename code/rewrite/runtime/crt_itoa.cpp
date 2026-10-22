// _itoa 家族逐字节复刻 —— batch 1043
//
// 0x7D46B9(92B) = _itoa 核心，机器码语义：
//   push ebp / mov ebp,esp / cmp [ebp+0x14],0(sign) / mov ecx,[ebp+0xc]
//   (buffer) / push ebx,esi,edi / je 0x7D46D3
//     ; sign != 0 分支
//     mov esi,[ebp+8](value) / mov byte [ecx],0x2d('-') / inc ecx /
//     neg esi                        ; ★无条件无符号取负
//     jmp 0x7D46D6
//   0x7D46D3: ; sign == 0 分支
//     mov esi,[ebp+8](value)         ; 位型原样（无符号）
//   0x7D46D6: mov edi,ecx            ; ★edi = 数字区起点（负号之后）
//   0x7D46D8 循环:
//     mov eax,esi / xor edx,edx / div [ebp+0x10](radix)  ; div1
//     mov eax,esi                    ; ★eax 重置（div1 商丢弃）
//     mov ebx,edx                    ; ★ebx = 余数（div1 的 edx）
//     xor edx,edx / div [ebp+0x10]   ; ★div2 重算商（edx 重新覆盖）
//     cmp ebx,9 / mov esi,eax        ; ★esi = div2 商
//     jbe 0x7D46F4 / add bl,0x57     ; 余>9 小写 hex（10+0x57=0x61='a'）
//       / jmp 0x7D46F7
//     0x7D46F4: add bl,0x30          ; 余<=9 '0'+余
//     0x7D46F7: mov byte [ecx],bl / inc ecx
//     test esi,esi / ja 0x7D46D8     ; 商 != 0 继续（无符号）
//   0x7D46FE: and byte [ecx],0       ; NUL
//   dec ecx                          ; ★ecx = 最后一个数字
//   0x7D4702 反转循环（无条件先执行一次）:
//     mov dl,[edi] / mov al,[ecx]    ; ★al = 残留（最后取 [ecx] 的字节）
//     mov [ecx],dl / mov [edi],al / dec ecx / inc edi
//     cmp edi,ecx / jb 0x7D4702      ; 无符号 edi < ecx 继续
//   pop edi,esi,ebx / pop ebp / ret
//
// ★怪癖（golden 实证，照抄）：
//   ① div 两遍——div1 的商被 `mov eax,esi` 丢弃、余数经 ebx 保留；
//     div2 重算商进 esi（edx 同样重算但不用）——数学上等价、寄存器
//     节奏不同，照抄结构；
//   ② 余数 >9 → +0x57（小写 hex）；<=9 → +0x30；
//   ③ sign!=0 无条件 neg（**正数也会被取负**——golden:
//      2147483647, sign=1 → "-2147483649"）；
//   ④ sign==0 时 value 按无符号位型除（_ultoa 语义——golden:
//      -12345 → "4294954951"）；
//   ⑤ 反转循环先执行一次（do-while），并可能使 ecx 越过 buffer 前缘
//      一次（edi > ecx 即停）——单数字时 ecx 减到 buffer-1 但不再解
//      引用，安全；
//   ⑥ 返回 eax = 反转循环最后一次 `mov al,[ecx]` 的低字节（高 24 位
//      为 div 循环退出时的商残值 0）——调用方（_ultoa/_ltoa）自恢复
//      buffer 指针，核心返回约定不依赖。

#include "crt_itoa.h"

namespace ra2::crt {

std::uint32_t itoa_core_legacy(std::int32_t value, char* buffer,
                               std::int32_t radix, std::int32_t sign) {
    char* ecx = buffer;
    std::uint32_t esi;
    if (sign != 0) {
        *ecx = '-';
        ++ecx;
        esi = 0u - static_cast<std::uint32_t>(value);  // neg 无条件
    } else {
        esi = static_cast<std::uint32_t>(value);       // 位型原样
    }
    char* edi = ecx;                                    // 数字区起点
    const std::uint32_t radix_u = static_cast<std::uint32_t>(radix);
    std::uint32_t ebx;
    do {
        // div1 求余（ebx）；div2 重算商（esi）——数学同值、节奏照抄
        ebx = esi % radix_u;
        esi = esi / radix_u;
        if (ebx > 9) {
            *ecx = static_cast<char>(ebx + 0x57);       // 小写 hex
        } else {
            *ecx = static_cast<char>(ebx + 0x30);
        }
        ++ecx;
    } while (esi != 0);
    *ecx = 0;
    --ecx;
    std::uint32_t al = 0;
    do {
        const char dl = *edi;
        al = static_cast<std::uint8_t>(*ecx);
        *ecx = dl;
        *edi = static_cast<char>(al);
        --ecx;
        ++edi;
    } while (reinterpret_cast<std::uintptr_t>(edi) <
             reinterpret_cast<std::uintptr_t>(ecx));
    return al;                                          // eax 残留（高 24 位 0）
}

char* ultoa_legacy(std::uint32_t value, char* buffer, std::int32_t radix) {
    itoa_core_legacy(static_cast<std::int32_t>(value), buffer, radix, 0);
    return buffer;
}

char* ltoa_legacy(std::int32_t value, char* buffer, std::int32_t radix) {
    const std::int32_t sign = (radix == 10 && value < 0) ? 1 : 0;
    itoa_core_legacy(value, buffer, radix, sign);
    return buffer;
}

}  // namespace ra2::crt