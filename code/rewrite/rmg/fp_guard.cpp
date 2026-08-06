#include "fp_guard.h"

namespace ra2::rmg {

namespace {
// volatile 让编译期常量折叠无法把探针整个吃掉——否则探到的是编译器的舍入而非 CPU 的
volatile double g_one = 1.0;
volatile double g_ten = 10.0;
}  // namespace

bool FpuRoundingModeWorks() {
  const double a = g_one;
  const double b = g_ten;

  const int saved = std::fegetround();
  std::fesetround(FE_TONEAREST);
  const double nearest = a / b;
  std::fesetround(FE_TOWARDZERO);
  const double chop = a / b;
  std::fesetround(saved);

  // 0.1 的十进制值落在两个 double 之间且更靠近上方：
  //   就近舍入 -> 0x3FB999999999999A（比 0.1 大）
  //   向零截断 -> 0x3FB9999999999999（比 0.1 小）
  // 两者必须不同，否则说明 fesetround 被忽略了（多半是 -ffast-math）。
  return nearest != chop;
}

}  // namespace ra2::rmg
