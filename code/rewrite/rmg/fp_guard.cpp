#include "fp_guard.h"

namespace ra2::rmg {

namespace {
// volatile 让编译期常量折叠无法把探针整个吃掉——否则探到的是编译器的舍入而非 CPU 的
volatile double g_one = 1.0;
volatile double g_ten = 10.0;

// 优化屏障：告诉编译器「这个 double 已被改动、内容未知」。
// 缺了它，两次 1.0/10.0 会被 GCC 折叠/复用成同一个编译期常量，
// 探针恒假——而它恰恰是专门用来发现「舍入模式被编译器架空」的检查，
// 自己被架空就失去了意义。CI ubuntu 上 fast_math / map_size 两个
// 舍入探针即栽在这（-frounding-math 不足以阻止此处折叠）。
// MSVC 侧靠 /fp:strict 本就通过，这里同样加上以防将来换工具链。
inline void OpaqueBarrier(double& v) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : "+x"(v));
#else
  (void)v;
#endif
}
}  // namespace

bool FpuRoundingModeWorks() {
  const int saved = std::fegetround();

  double a = g_one;
  OpaqueBarrier(a);
  std::fesetround(FE_TONEAREST);
  const double nearest = a / g_ten;

  a = g_one;
  OpaqueBarrier(a);
  std::fesetround(FE_TOWARDZERO);
  const double chop = a / g_ten;

  std::fesetround(saved);

  // 0.1 的十进制值落在两个 double 之间且更靠近上方：
  //   就近舍入 -> 0x3FB999999999999A（比 0.1 大）
  //   向零截断 -> 0x3FB9999999999999（比 0.1 小）
  // 两者必须不同，否则说明 fesetround 被忽略了（多半是 -ffast-math）。
  return nearest != chop;
}

}  // namespace ra2::rmg
