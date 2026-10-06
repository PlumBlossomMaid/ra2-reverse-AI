// 测试 x87 fyl2x 输出是否正确
#include <cstdio>
#include <cmath>
#include "x87_fyl2x.h"
#include "dd_log.h"

int main() {
    double test[] = {0.5, 0.1, 0.9, 0.3, 0.7, 0.01, 0.99, 0.001, 0.5e-10, 0.999};
    for (double x : test) {
        double x87 = ra2::rmg::X87Fyl2x::Instance().Compute(x);
        double portable = ra2::rmg::dd::ChopLogPortable(x);
        double ref = std::log(x);
        printf("x=%.17g  x87=%.17g  portable=%.17g  std::log=%.17g  diff=%d\n",
               x, x87, portable, ref, x87 != portable ? 1 : 0);
    }
    // 特别测试：NormalDeviate 典型 s 值范围 [0.01, 1.0)
    int diffs = 0;
    for (int i = 1; i < 10000; ++i) {
        double x = static_cast<double>(i) / 10000.0;
        double x87 = ra2::rmg::X87Fyl2x::Instance().Compute(x);
        double portable = ra2::rmg::dd::ChopLogPortable(x);
        if (x87 != portable) {
            ++diffs;
            if (diffs <= 10)
                printf("  diff at x=%.17g: x87=%.17a portable=%.17a\n", x, x87, portable);
        }
    }
    printf("10000 samples: %d diffs\n", diffs);
    return 0;
}
