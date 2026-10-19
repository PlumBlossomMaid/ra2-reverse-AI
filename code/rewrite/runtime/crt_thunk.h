#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x7C9BFD（11B，ranking.tsv uc=29 / transitive_dependents=861，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：CRT 数字解析
// 转发 thunk——cdecl 1 栈参原样转发给 0x7C9B72、ret 由调用者清栈。
// 依赖 0x7C9B72（MSVCRT strtol/atoi 族十进制前缀解析，已有 REIMPL
// parse_decimal_legacy，lowlevel）——batch 948 以桩注入返回值，
// 验证 thunk 的转发语义。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 8 用例实证，
// memory/data/rmg/deadcode_7c9bfd_golden.txt）：
//
//   push [esp+4]      ; 转发第一参（复制参数槽内容）
//   call 0x7C9B72     ; eax = parse_decimal(arg)
//   pop ecx           ; 清转发参（cdecl 调用者清理）
//   ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① 第一参原样复制转发（push [esp+4]），非原地使用——目标函数可
//      安全修改自己的栈参槽而不影响调用者栈；
//   ② 全路径必然调用一次目标（arg 任意——无 NULL 短路）；
//   ③ 返回值 eax 透传（包装本身不改）。

// 外部语义注入点（目标 0x7C9B72 语义与调用约定按原版固定）
struct CrtThunkWorld {
    std::uint32_t (*target)(std::uint32_t arg);  // 0x7C9B72
};

// 0x7C9BFD：数字解析转发 thunk。arg 为任意 32 位值（原样转发）。
std::uint32_t parse_thunk_legacy(CrtThunkWorld* world, std::uint32_t arg);

}  // namespace ra2::runtime