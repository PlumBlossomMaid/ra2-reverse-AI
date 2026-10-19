#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x7C978A（18B，ranking.tsv uc=37 / transitive_dependents=1156，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：MSVCRT 布尔
// 规范化包装——cdecl 1 栈参 p、ret 由调用者清栈。依赖 0x7C970C
// （0x40+ 长，内部堆检查/realloc 链，batch 946 以桩注入返回值）。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 8 用例实证，
// memory/data/rmg/deadcode_7c978a_golden.txt）：
//
//   push [esp+4]        ; p 转发（复制参数）
//   call 0x7C970C       ; eax = inner(p)
//   neg eax             ; eax = -eax（CF=1 除非原 eax==0）
//   sbb eax, eax        ; eax = -CF = (原≠0) ? -1 : 0
//   pop ecx             ; 清转发参数
//   neg eax             ; (原≠0) ? 1 : 0
//   dec eax             ; (原≠0) ? 0 : -1
//   ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① 最终语义：inner(p) 非零 → 返回 0；inner(p) 为零 → 返回 -1
//      （neg/sbb/neg/dec 四步布尔规范化，非简单三元判断）；
//   ② p 被复制转发（push [esp+4]）而非原地使用——inner 可安全修改
//      自己的栈参槽；
//   ③ 全路径必然调用一次 inner（NULL 或任意 p 都转发）。

// 外部语义注入点（inner 语义与参数按原版调用约定）
struct CrtBoolNormWorld {
    std::uint32_t (*inner)(std::uintptr_t p);  // 0x7C970C
};

// 0x7C978A：布尔规范化。p 为任意指针（原样转发给 inner）。
std::int32_t bool_normalize_legacy(CrtBoolNormWorld* world,
                                   std::uintptr_t p);

}  // namespace ra2::runtime