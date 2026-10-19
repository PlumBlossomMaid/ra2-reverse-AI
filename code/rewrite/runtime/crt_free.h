#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7C93E8（72B，ranking.tsv uc=55 / transitive_dependents=1217，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：MSVCRT `free`
// 的调度入口——cdecl 1 栈参 p、ret 由调用者清栈、返回 void。
// 依赖（全部为桩注入的外部语义，见 CrtFreeWorld）：
//   0x7CD9F5 EnterLock(9)   —— CRT region lock 入口（54B，含 CS 懒分配链）
//   0x7CDA56 LeaveLock(9)   —— CRT region lock 出口（18B）
//   0x7CF7BD FindHeapSegment(p) —— 1MB 堆段包含扫描（已有 REIMPL
//             find_heap_segment_legacy，lowlevel）
//   0x7CF7E8 FreeSegment(seg, p) —— 堆段释放合并（811B，semantics partial）
//   [0xB78B9C] heap handle、[0x7E127C] IAT HeapFree（kernel32 stdcall）
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 5 用例实证，
// memory/data/rmg/deadcode_7c93e8_golden.txt）：
//
//   push esi / mov esi,[esp+8]（p）/ test esi,esi / je 直接返回
//   push 9 / call 0x7CD9F5        ; EnterLock(9)，参数后由 pop 清理
//   push esi / call 0x7CF7BD      ; seg = FindHeapSegment(p)
//   pop ecx / test eax,eax / pop ecx   ; 清 Find 参 + EnterLock 参
//   je 0x7C9417（未命中）
//   命中（0x7C9404）：
//     push esi(p) / push eax(seg) / call 0x7CF7E8   ; FreeSegment(seg,p)
//     push 9 / call 0x7CDA56      ; LeaveLock(9)
//     add esp,0xC / pop esi / ret  ; 清 FreeSeg 2 参 + Leave 1 参
//   未命中（0x7C9417）：
//     push 9 / call 0x7CDA56      ; LeaveLock(9)
//     pop ecx                     ; 清 Leave 参
//     push esi(p) / push 0 / push [0xB78B9C](heap) / call [0x7E127C]
//                                 ; HeapFree(heap, 0, p)（stdcall 3 参）
//     pop esi / ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① p == NULL → 不碰锁/堆，直接返回（不调任何桩）；
//   ② 命中/未命中都在临界区内完成查找，未命中路径先 LeaveLock 再
//      HeapFree（释放系统堆段不持 CRT 锁）；
//   ③ EnterLock/LeaveLock 固定 key=9；
//   ④ HeapFree 参数 = ([0xB78B9C], 0, p)——flags 恒 0，heap 恒全局。

// 外部语义注入点（结构与参数顺序按原版汇编固定）
struct CrtFreeWorld {
    void (*enter_lock)(int which);       // 0x7CD9F5
    void (*leave_lock)(int which);       // 0x7CDA56
    void* (*find_segment)(void* p);      // 0x7CF7BD
    void (*free_segment)(void* seg, void* p);  // 0x7CF7E8
    void (*heap_free)(void* heap, unsigned flags, void* p);  // HeapFree
    void* heap_handle;                   // [0xB78B9C]
};

// 0x7C93E8：free。p 为要释放的指针（可为 NULL）。
void free_legacy(CrtFreeWorld* world, void* p);

}  // namespace ra2::runtime