#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7C9430（18B，ranking.tsv uc=37 / transitive_dependents=1156，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：MSVCRT `malloc`
// 入口包装——cdecl 1 栈参 size、ret 由调用者清栈。依赖 [0xB782C4]
// 全局（堆标志/retry-hook）与 0x7C9442（malloc 核心入口：大小阈值
// cmp [esp+4],-0x20 + 常规分配 0x7C946E + 重试循环 0x7D06E8，含锁 9，
// batch 947 以桩注入返回值）。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 8 用例实证，
// memory/data/rmg/deadcode_7c9430_golden.txt）：
//
//   push [0xB782C4]      ; 压全局（堆标志/retry-hook）
//   push [esp+8]         ; 转发 size（push 后 size 从 [esp+4] 移到 [esp+8]）
//   call 0x7C9442        ; eax = core(size, 全局值)
//   pop ecx / pop ecx    ; 清 2 参（cdecl 调用者清理）
//   ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① 参数顺序：core 收 (size, 全局值)——push 全局先压、push size 后压，
//      故 [esp+4]=size（最后 push）、[esp+8]=全局值；
//   ② 全路径必然调用一次 core（size 任意、全局任意——无 NULL 短路）；
//   ③ 返回值 = core 返回值（eax 透传，包装本身不改）；该包装不检查
//      core 是否返回 NULL——失败语义在 core 内部。

// 外部语义注入点（core 语义与参数顺序按原版调用约定）
struct CrtMallocWorld {
    void* (*core)(std::size_t size, std::uintptr_t flag);  // 0x7C9442
    std::uintptr_t flag;                                   // [0xB782C4]
};

// 0x7C9430：malloc 入口。size 为请求字节数（可为 0）。
void* malloc_legacy(CrtMallocWorld* world, std::size_t size);

}  // namespace ra2::runtime