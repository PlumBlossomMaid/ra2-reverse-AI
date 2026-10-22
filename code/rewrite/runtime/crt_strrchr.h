#pragma once

namespace ra2::crt {

// 0x7C8DF0 MSVCRT strrchr（反向字符查找，cdecl 2 栈参 (s, c)，返回 s 中
// **最后一次**出现字符 c 的指针、未找到返 0；★c==0 返终止 NUL 指针——
// 标准 strrchr 语义）。机器码 39B（0x7C8DF0-0x7C8E16）：
//   push ebp / mov ebp,esp / push edi
//   mov edi,[ebp+8]=s
//   xor eax,eax（AL=0）→ or ecx,-1 → repne scasb（★正扫 NUL）
//   inc ecx → neg ecx（★ECX = 串长含 NUL = len+1）
//   dec edi（★指向 NUL 处）
//   mov al,[ebp+0xC]=c（★只取低 8 位）
//   std（★方向标志=递减）
//   repne scasb（★反向从 NUL 处倒找 c，共 len+1 个位置）
//   inc edi（★校正：REPNE 停止时 EDI 已越位）
//   cmp [edi],al → je 0x7C8E11（命中返指针）
//   xor eax,eax（未命中返 0）
//   0x7C8E11: mov eax,edi
//   cld（★恢复 DF）→ pop edi → leave → ret
// ★怪癖：①反向扫描覆盖 [len..0] 共 len+1 位置（含 NUL 与 s[0]）；
//   ②收尾 cmp 在未命中（ECX 耗尽）时把 s[0] 再比一次——s[0]==c 仍返
//   &s[0]（反向最后检查位），反证返回指针确为 c；③c 只取低 8 位（高
//   位忽略）；④c==0 时 NUL 处即命中返终止 NUL 指针（含空串）；⑤
//   std/cld 配对（cdecl 要求 DF 返回时 clear）；⑥or ecx,-1 编码（非
//   mov ecx,-1）。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7c8df0_golden.txt，
// 17 用例）。
const char* strrchr_legacy(const char* s, int c);

}  // namespace ra2::crt