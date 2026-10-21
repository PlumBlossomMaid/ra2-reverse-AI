#include "dead_code_58c320.h"

#include "scaled_random.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ra2 {
namespace rmg {

// 0x58C320（142B，thiscall+1 栈参 ret 4——ecx=this 读 [this+8]、[esp+4]=
// 输出指针）：
//   side  = [0x89C2DC]；total = side*side（imul）
//   scale = (double)(uint64)total       （fild qword 无符号读）
//   loop: r = Random()（0xABE890 全局）→ v = _ftol((double)(uint64)r * scale
//             * kInvUintMax)            （RC=chop 链）
//         if ((unsigned)v > total-1) goto loop      （ja 拒绝采样）
//         p = [0xABED10] + v*0x50
//         if (p->[0x38] != this->[8]) goto loop     （★类型不匹配重抽——
//             先于空格判定，次序照抄）
//         if (p->x == 0 && p->y == 0) goto loop     （空格子重抽）
//         *out = work[v] 首 dword（x | (y<<16)）
// C++ 侧把输出作为返回值返回（golden 比 work[v] 首 dword）。
std::uint32_t DeadCode58C320(const DeadCode58C320Context& ctx) {
  const std::uint32_t side = static_cast<std::uint32_t>(ctx.side);
  const std::uint64_t total = static_cast<std::uint64_t>(side) * side;
  const double scale = static_cast<double>(total);  // fild qword 无符号读

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
      continue;  // ja 无符号拒绝采样
    }
    if (v < 0 || static_cast<std::uint32_t>(v) >= ctx.work.size()) {
      continue;  // 防御：work 越界（原版 golden 内不会发生）
    }
    const DeadCode58C320Rec& p = ctx.work[static_cast<std::uint32_t>(v)];
    if (p.field38 != ctx.this8) {
      continue;  // ★类型不匹配重抽（0x58C382，先于空格判定）
    }
    if ((p.coord & 0xFFFF) == 0 && ((p.coord >> 16) & 0xFFFF) == 0) {
      continue;  // 空格子 (0,0) 重抽
    }
    return p.coord;  // x | (y<<16)
  }
}

}  // namespace rmg
}  // namespace ra2