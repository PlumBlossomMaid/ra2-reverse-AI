#include "crt_qsort.h"

#include <vector>

namespace ra2::runtime {
namespace {

std::uint8_t* as_u8(void* p) { return static_cast<std::uint8_t*>(p); }

// 0x7C8CEA：swap(a, b, n)——a==b 或 n==0 直接返回，否则逐字节交换。
void swap_bytes(void* a, void* b, std::size_t n) {
    if (a == b || n == 0) return;
    std::uint8_t* pa = as_u8(a);
    std::uint8_t* pb = as_u8(b);
    for (std::size_t k = 0; k < n; ++k) {
        std::uint8_t t = pa[k];
        pa[k] = pb[k];
        pb[k] = t;
    }
}

// 0x7C8C9C：收缩排序——每轮在 [first+size, last] 找「cmp(cur,first)>0」
// 的最后一个位置（无则 first），与 last 交换，last -= size 直至 last<=first。
void shrink_sort(std::uint8_t* first, std::uint8_t* last, std::size_t size,
                 QsortCompare cmp) {
    if (last <= first) return;
    std::uint8_t* const cur0 = first + size;
    for (;;) {
        std::uint8_t* rec = first;
        std::uint8_t* cur = cur0;
        while (cur <= last) {
            // 汇编 0x7C8CC9 `mov ebx, esi`：基准随 rec 动态更新——
            // 比较是 cmp(cur, rec)，不是固定 first
            if (cmp(cur, rec) > 0) rec = cur;
            cur += size;
        }
        swap_bytes(rec, last, size);
        last -= size;
        if (!(last > first)) return;
    }
}

}  // namespace

void qsort_legacy(void* base, std::size_t count, std::size_t size,
                  QsortCompare cmp) {
    if (count < 2 || size == 0) return;

    std::uint8_t* first = as_u8(base);
    std::uint8_t* last = first + (count - 1) * size;

    // 原版区间栈（[ebp-0xF8] 存 first、[ebp-0x80] 存 last，深度 [ebp-4]）
    std::vector<std::uint8_t*> stack_first;
    std::vector<std::uint8_t*> stack_last;

    for (;;) {
        const std::size_t n =
            static_cast<std::size_t>(last - first) / size + 1;
        if (n <= 8) {
            shrink_sort(first, last, size, cmp);
        } else {
            // 快排分区：中点元素交换到 first 作枢轴
            std::uint8_t* const mid = first + (n / 2) * size;
            swap_bytes(mid, first, size);

            std::uint8_t* i = first;
            std::uint8_t* j = last + size;  // 右扫先减再判
            for (;;) {
                // 左扫：i += size 后判 i > last；否则 cmp(i, first) > 0 停
                for (;;) {
                    i += size;
                    if (i > last) break;
                    if (cmp(i, first) > 0) break;
                }
                // 右扫：j -= size 后判 j <= first；否则 cmp(j, first) < 0 停
                for (;;) {
                    j -= size;
                    if (j <= first) break;
                    if (cmp(j, first) < 0) break;
                }
                if (j >= i) {
                    swap_bytes(i, j, size);
                    continue;  // 回左扫（i 立即再 += size）
                }
                break;  // 枢轴归位路径
            }
            swap_bytes(first, j, size);

            // 区间调度：ecx = j-first-1（字节）、edx = last-i（字节）
            const std::ptrdiff_t left_bytes = (j - first) - 1;
            const std::ptrdiff_t right_bytes = last - i;
            if (left_bytes < right_bytes) {
                // 左区间小：右区间 [i,last] 入栈，继续左区间 [first,j-size]
                if (i < last) {
                    stack_first.push_back(i);
                    stack_last.push_back(last);
                }
                if (first + size >= j) {
                    // 左区间空 → 出栈
                } else {
                    last = j - size;
                    continue;
                }
            } else {
                // 右区间小：左区间 [first,j-size] 入栈，继续右区间 [i,last]
                if (first + size < j) {
                    stack_first.push_back(first);
                    stack_last.push_back(j - size);
                }
                if (i >= last) {
                    // 右区间空 → 出栈
                } else {
                    first = i;
                    continue;
                }
            }
        }

        // 0x7C8BA6：出栈；栈空返回
        if (stack_first.empty()) return;
        first = stack_first.back();
        stack_first.pop_back();
        last = stack_last.back();
        stack_last.pop_back();
    }
}

}  // namespace ra2::runtime