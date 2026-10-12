#include "fp_guard.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace ra2::rmg {

namespace {
// 输入与结果都要 volatile：只把输入标 volatile 不够——除法结果若能留在
// 寄存器里，编译器仍可跨两次 fesetround 复用同一次除法。给结果加 volatile
// 强制它真实落盘，两次舍入的结果才可能不同。
// 实测（ubuntu-latest CI）：仅输入 volatile 时本探针返回 false，
// 输入+结果均 volatile 后通过（见 fp_guard 的 CI 修复提交）。
volatile double g_one = 1.0;
volatile double g_ten = 10.0;

// 编译屏障：禁止把除法重排到 fesetround 之外。
inline void CompilerBarrier() {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

unsigned long long Bits(double v) {
  unsigned long long b;
  std::memcpy(&b, &v, sizeof b);
  return b;
}
}  // namespace

bool FpuRoundingModeWorks() {
  const int saved = std::fegetround();

  const int rc_near = std::fesetround(FE_TONEAREST);
  CompilerBarrier();
  volatile double nearest = g_one / g_ten;
  const int mode_near = std::fegetround();

  const int rc_chop = std::fesetround(FE_TOWARDZERO);
  CompilerBarrier();
  volatile double chop = g_one / g_ten;
  const int mode_chop = std::fegetround();

  std::fesetround(saved);

  const bool ok = (nearest != chop);
  if (!ok) {
    // 探针的职责就是把「舍入模式被架空」变成可诊断的事实，否则下游对拍
    // 会以极难定位的方式失败。这里把四个判据一起打出来：
    // rc_* 区分 fesetround 调用本身失败，mode_* 区分模式没设上，
    // 两个位形区分除法被复用。三者组合可唯一定位。
    std::fprintf(stderr,
                 "[fp_guard] FAIL rc_near=%d rc_chop=%d mode_near=%d "
                 "mode_chop=%d nearest=%016llx chop=%016llx "
                 "(expect nearest=3fb999999999999a chop=3fb9999999999999)\n",
                 rc_near, rc_chop, mode_near, mode_chop,
                 Bits(nearest), Bits(chop));
  }
  return ok;
}

}  // namespace ra2::rmg
