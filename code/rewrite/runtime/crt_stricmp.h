#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7C8D20（208B，205 unique_callers / 3932 transitive_dependents，
// ranking.tsv 扇入度最高的未复刻 CRT 项）：MSVC6 静态 CRT 大小写不敏感
// 字节串比较 `_stricmp`（strcmpi）。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 15 用例实证，
// memory/data/rmg/deadcode_7c8d20_golden.txt）：
//
//   push ebp / mov ebp,esp / push edi / push esi / push ebx
//   esi=[ebp+0xC]（s2）；edi=[ebp+8]（s1）
//   cmp [0xB782A0],0        ; locale 全局：==0 快路径 / !=0 慢路径
//   jne 慢路径
// 快路径：
//   mov al,0xFF             ; 哨兵（首轮 or al,al 非零保证进入循环）
// 循环顶 or al,al / je 返回 al ；al 为上一轮读到的 s2 字符
//   mov al,[esi]++          ; al = *s2++
//   mov ah,[edi]++          ; ah = *s1++
//   cmp ah,al / je 循环顶    ; 原始相等 → 继续
//   ; 大小写折叠 s2 字符（al）：
//   sub al,0x41 / cmp al,0x1A / sbb cl,cl / and cl,0x20 / add al,cl / add al,0x41
//   xchg al,ah              ; al=s1 原始字符、ah=s2 折叠后
//   ; 大小写折叠 s1 字符（al）：
//   sub al,0x41 / cmp al,0x1A / sbb cl,cl / and cl,0x20 / add al,cl / add al,0x41
//   cmp al,ah / je 循环顶    ; 折叠后相等 → 继续（al 现在=折叠 s1 字符）
//   sbb al,al / sbb al,0xFF  ; 折叠 s1 < 折叠 s2（无符号）→ -1，否则 +1
//   movsx eax,al / ret
// 慢路径（[0xB782A0]!=0，locale 激活）：
//   lock inc [0xB78BA4]      ; 锁计数++
//   cmp [0xB78BA0],0 / jg 重入
//   push 0（锁状态 0）→ 正常进入
//   重入分支：lock dec 回退 / push 0x13 / call 0x7CD9F5（报错，不触发）
//   循环同快路径，但字符不等时：
//     push eax（存 s2 字符）/ push ebx（第一参=s1 字符）
//     call 0x7CB063          ; tolower(s1) → eax；mov ebx,eax
//     call 0x7CB063          ; 第二参复用栈上保存的 s2 字符 → tolower(s2)
//     cmp bl,al / je 循环顶   ; tolower(s1) vs tolower(s2)
//     sbb eax,eax / sbb eax,-1 ; s1<s2 → -1 否则 +1
//   pop eax（锁状态）/ or / jne 异常解锁(0x7CDA56) / lock dec [0xB78BA4]
//   mov eax,ebx / ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① 循环顶 `or al,al` 判定的是**上一轮读到的 s2 字符**——首轮用哨兵
//      al=0xFF 保证进入；s2 先到 NUL 且与 s1 全等 → 返回 0；
//   ② 大小写折叠仅 'A'..'Z'（al-0x41 无符号 < 0x1A）加 0x20，用
//      sbb cl,cl / and cl,0x20 惯用法；'@'(0x40) 减 0x41 回绕成 0xFF
//      后仍原样（非字母不动）；
//   ③ 比较结果 = 折叠 s1 < 折叠 s2（无符号）→ -1，否则 +1（sbb 双链）；
//   ④ 慢路径 tolower 调用序：**先 s1 后 s2**（push eax 存 s2、push ebx
//      作第一参，第二次调用复用栈上 s2）——与快路径的折叠顺序相反；
//   ⑤ 慢路径锁 [0xB78BA4] +1 后 -1 平衡（golden lockf=0 locka=0）；
//   ⑥ 重入分支（[0xB78BA0]>0）与 0x7CD9F5/0x7CDA56 在 golden 中 0 次
//      调用——锁未持有时不触发（C++ 侧保留接口对齐结构）。

// 这一阶段要问外界的事（原版侧：[0xB782A0] locale 全局、0x7CB063
// tolower、0x7CD9F5 重入报错、0x7CDA56 异常解锁、[0xB78BA0]/[0xB78BA4]
// 锁状态；C++ 侧抽象成世界回调以便对拍）。
class CrtStricmpWorld {
 public:
    virtual ~CrtStricmpWorld() {}

    // [0xB782A0] != 0 → locale 激活（慢路径）
    virtual bool LocaleActive() const = 0;

    // 0x7CB063 tolower：慢路径逐字符转换（本批桩边界，golden 桩 =
    // ASCII tolower：'A'..'Z' → +0x20）
    virtual std::uint32_t LocaleToLower(std::uint32_t ch) = 0;

    // 慢路径锁：Acquire = lock inc [0xB78BA4]（重入时先 dec 再 0x7CD9F5
    // 报错）；Release = lock dec [0xB78BA4]（异常时 0x7CDA56）
    virtual void AcquireLock() = 0;
    virtual void ReleaseLock() = 0;
};

// 0x7C8D20：stricmp 复刻。s1/s2 为 NUL 结尾字节串。返回
// 折叠 s1 < 折叠 s2 → -1，相等 → 0，> → +1。
int stricmp_legacy(CrtStricmpWorld& world, const char* s1, const char* s2);

// 快路径 ASCII 折叠单字符（原版 sbb/and 惯用法等价）：
// 仅 'A'..'Z'（c-0x41 < 0x1A，无符号）加 0x20，其余原样。
std::uint8_t stricmp_ascii_fold(std::uint8_t c);

// 原版栈上初始化、从未参与计算的上限常量（golden 实证 js 永不成立）
inline constexpr std::uint32_t kStricmpLocaleGlobal = 0xB782A0;

}  // namespace ra2::crt