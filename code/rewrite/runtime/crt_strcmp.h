#pragma once

namespace ra2::crt {

// 0x7CDA90 MSVCRT strcmp（字节串比较，cdecl 2 栈参 (s1, s2)，三态返回
// {-1,0,+1}，无符号逐字节比较，大小写敏感）。机器码 129B+2B 尾部
// （0x7CDA90-0x7CDB13）：
//   mov edx,[esp+4]=s1 / mov ecx,[esp+8]=s2
//   test edx,3 → jne 0x7CDADC      ; ★只检查 s1 对齐
//   快路径（0x7CDAA0，s1 已 4 对齐）：
//     循环：mov eax,[edx]（★4 字节小端窗口：al=b0, ah=b1, hi16=b2,b3）
//       cmp al,[ecx]     → jne 0x7CDAD4（三态）
//       or al,al         → je 0x7CDAD0（b0==0 → 返 0）
//       cmp ah,[ecx+1]   → jne 0x7CDAD4
//       or ah,ah         → je 0x7CDAD0
//       shr eax,0x10     ; ★高 16 位移低：al=b2, ah=b3
//       cmp al,[ecx+2]   → jne 0x7CDAD4
//       or al,al         → je 0x7CDAD0
//       cmp ah,[ecx+3]   → jne 0x7CDAD4
//       add ecx,4 / add edx,4
//       or ah,ah         → jne 循环（b3!=0 继续）
//     （b3==0 → 落到 0x7CDAD0）
//   0x7CDAD0: xor eax,eax / ret（相等且 s1 到 NUL → 0）
//   0x7CDAD4: sbb eax,eax / shl eax,1 / inc eax / ret
//     ★三态出口：CF=1（s1<b2）→ -1；CF=0（s1>b2）→ +1
//   慢路径（0x7CDADC，s1 未 4 对齐）：
//     test edx,1 → je 0x7CDAF8（偶 → 2 字节路径）
//     1 字节路径（0x7CDAE4）：mov al,[edx] / inc edx /
//       cmp al,[ecx] → jne 三态 / inc ecx / or al,al → je 返 0 /
//       test edx,2 → je 0x7CDAA0（★已 4 对齐 → 进快循环）→ fallthrough
//     2 字节路径（0x7CDAF8）：mov ax,[edx] / add edx,2 /
//       cmp al,[ecx] → jne 三态 / or al,al → je 返 0 /
//       cmp ah,[ecx+1] → jne 三态 / or ah,ah → je 返 0 /
//       add ecx,2 → jmp 0x7CDAA0（进快循环）
// ★怪癖：①只有 s1 参与对齐检查，s2 全单字节访问（不对齐）；②窗口读
//   mov eax,[edx] 4 字节原子加载，NUL 之后仍越界读 1-3 字节（安全窗口
//   假设，NUL 检查短路不参与比较）；③shr eax,0x10 后 al=b2/ah=b3；
//   ④三态 sbb/shl/inc = {-1,0,+1}；⑤b3==0 检查在 add 指针之后。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7cda90_golden.txt，
// 20 用例 × 4 对齐 = 80 组）。
int strcmp_legacy(const char* s1, const char* s2);

}  // namespace ra2::crt