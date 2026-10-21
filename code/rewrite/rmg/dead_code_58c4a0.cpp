#include "dead_code_58c4a0.h"

#include "scaled_random.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ra2 {
namespace rmg {

// 0x58C4A0（231B，thiscall+1 栈参 ret 4——ecx=this 读 [this+8]、[esp+4]=
// 输出指针）：
//   side  = [0x89C2DC]；total = side*side（imul）
//   scale = (double)(uint64)total       （fild qword 无符号读）
//   loop: r = Random()（0xABE890 全局）→ v = _ftol((double)(uint64)r * scale
//             * kInvUintMax)            （RC=chop 链）
//         if ((unsigned)v > total-1) goto loop      （ja 拒绝采样）
//         ★rep movsd 0x14 dword（80B）把 work[v] 复制到 work[0]（edi=ebp=
//           [0xABED10] work 基址）——判定/输出都读复制后 work[0]
//         ebx++（尝试计数；cmp ebx,0x64 / jge 失败出口——100 次上限）
//         if (work[0].[0x38] != this->[8]) goto loop （类型不匹配）
//         if (work[0].x == 0 && work[0].y == 0) goto loop （空格子）
//         if (work[0].[0x44] byte == 0) goto loop    （第三重前置判定）
//         cell = GetCellAt(0x5657A0, &work[0]) → if (0x486380(cell)==0)
//             goto loop                               （谓词判定）
//         *out = work[0] 首 dword（x | (y<<16)）
//   fail: *out = (0,0)（随机数不回退）
std::uint32_t DeadCode58C4A0(const DeadCode58C4A0Context& ctx) {
  const std::uint32_t side = static_cast<std::uint32_t>(ctx.side);
  const std::uint64_t total = static_cast<std::uint64_t>(side) * side;
  const double scale = static_cast<double>(total);  // fild qword 无符号读

  int attempts = 0;  // 原版 ebx：只对「通过拒绝采样的被接受采样」计数
  for (;;) {
    const std::uint32_t r = ctx.rng->Random();

#if defined(_M_X64) || defined(__x86_64__)
    const unsigned csr = _mm_getcsr();
    _mm_setcsr((csr & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO);
#endif
    volatile double t = static_cast<double>(static_cast<std::uint64_t>(r)) * scale;
    volatile double t2 = t * kInvUintMax;
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif

    const std::int32_t v = Ftol(t2);  // _ftol（向零截断，FPCW=0x0E7F）
    if (static_cast<std::uint32_t>(v) > static_cast<std::uint32_t>(total - 1)) {
      continue;  // ja 无符号拒绝采样（★不计数——回 0x58C4E1 只重取随机数）
    }
    if (v < 0 || static_cast<std::uint32_t>(v) >= ctx.work.size()) {
      continue;  // 防御：work 越界（原版 golden 内不会发生）
    }
    // ★rep movsd 复制 work[v] → work[0]（80B）；C++ 侧直接读 work[v] 等价
    const DeadCode58C4A0Rec& p = ctx.work[static_cast<std::uint32_t>(v)];

    // ★inc ebx（0x58C518）→ cmp ebx,0x64 / jge 失败出口（0x58C567）：
    //   第 100 次被接受的采样即失败，共消耗 100 次 Random（golden next 校验）。
    if (++attempts >= 100) {
      return 0;  // 失败出口 *out=(0,0)，不再判定、不额外采样
    }

    if (p.field38 != ctx.this8) {
      continue;  // 类型不匹配重抽
    }
    if ((p.coord & 0xFFFF) == 0 && ((p.coord >> 16) & 0xFFFF) == 0) {
      continue;  // 空格子 (0,0) 重抽
    }
    if (p.field44 == 0) {
      continue;  // field44==0 重抽（第三重前置判定）
    }

    // GetCellAt(&work[0]) → 0x486380 谓词（cell+0x38==0xFFFF||==0 通过）
    if (!ctx.world->CellPass(
            static_cast<int>(p.coord & 0xFFFF),
            static_cast<int>((p.coord >> 16) & 0xFFFF))) {
      continue;  // 谓词拦截重抽
    }
    return p.coord;  // x | (y<<16)
  }
}

}  // namespace rmg
}  // namespace ra2