#include "fp_guard.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace ra2::rmg {

namespace {
// volatile 读 + volatile 写：两端都堵死，编译器无法把除法折成编译期常量，
// 也无法把两次读取合并成同一次除法。
volatile double g_one = 1.0;
volatile double g_ten = 10.0;

// 编译屏障：禁止把除法跨越 fesetround 重排。
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
