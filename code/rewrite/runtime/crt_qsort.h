#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7C8B48（0x154 字节，ranking.tsv uc=60 / transitive_dependents=2106，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：MSVC6 静态 CRT
// 迭代式 qsort（cdecl 4 栈参 base/count/size/cmp，ret 由调用者清栈）。
// 零外部全局——子调用全在本段：0x7C8C9C 收缩排序、0x7C8CEA swap。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 14 用例实证，
// memory/data/rmg/deadcode_7c8b48_golden.txt）：
//
// qsort 主体（0x7C8B48-0x7C8C9B）：
//   count < 2 或 size == 0 → 直接返回（不调比较器）
//   first = base；last = base + (count-1)*size
//   显式区间栈（[ebp-0xF8] 存 first、[ebp-0x80] 存 last，深度计数 [ebp-4]）
//   主循环 0x7C8B87：
//     n = (last-first)/size + 1（无符号 div）
//     n <= 8 → 调 0x7C8C9C(first,last,size,cmp) 收缩排序 → 出栈
//              （depth<0 → 返回；否则弹 (first,last) 继续）
//     n > 8  → 快排分区：
//       mid = first + (n/2)*size；swap(mid, first, size)  （中点元素作枢轴）
//       左扫 i：i += size；i > last 停；cmp(i, first) > 0 停（否则继续）
//       右扫 j（初值 last+size）：j -= size；j <= first 停；
//              cmp(j, first) < 0 停（否则继续）
//       汇合：j < i → swap(first, j, size)（枢轴归位），随后按两侧
//             大小把「小区间」压栈、继续迭代「大区间」；
//             j >= i → swap(i, j, size)，回左扫
// 收缩排序（0x7C8C9C-0x7C8CE9）：
//   last <= first → 返回
//   每轮：rec = first；cur 从 first+size 到 last（含）：
//         cmp(cur, first) > 0 时 rec = cur
//         swap(rec, last, size)；last -= size；last > first 继续
// swap（0x7C8CEA-0x7C8D15）：
//   a == b 或 n == 0 → 返回；否则逐字节交换 a/b 的 n 字节
//
// ★原版怪癖（照抄）：
//   ① 比较器调用严格按上述顺序与实参（左扫 cmp(i,first)、右扫 cmp(j,first)、
//      收缩 cmp(cur,first)）——调用序列是可观测行为，必须逐位一致；
//   ② 枢轴取「中点元素」并交换到 first，不做三数取中；
//   ③ 右扫游标 j 初值 = last + size，先减再判（j <= first 直接汇合，
//      意味着枢轴为区间最大时不会比较 j == first）；
//   ④ 区间栈无溢出检查（原版固定栈区约 30 项，深度超限写坏栈帧）——
//      C++ 模型用动态栈，正常输入（深度 O(log n)）行为等价；
//   ⑤ swap 对 a == b 直接跳过（不做逐字节自交换）。

using QsortCompare = int (*)(const void*, const void*);

// 0x7C8B48：qsort。base 元素数 count、单元素 size 字节、cmp cdecl 比较器。
void qsort_legacy(void* base, std::size_t count, std::size_t size,
                  QsortCompare cmp);

}  // namespace ra2::runtime