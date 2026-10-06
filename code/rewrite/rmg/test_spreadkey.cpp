// 验证 X87SpreadKey 输出
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "scaled_random.h"
#include "x87_fyl2x.h"

int main() {
    // 先验证 X87Fyl2x 是否工作（排除 DEP/环境问题）
    double test_val = 0.5;
    double fyl2x_result = ra2::rmg::X87Fyl2x::Instance().Compute(test_val);
    printf("FYL2X(0.5) = %.17g (expected -0.693147)\n", fyl2x_result);

    // 再测 X87SpreadKey
    struct { double dist; uint32_t draw; } tests[] = {
        {0.0, 0},
        {1.0, 1000000},
        {5.0, 0xFFFFFFFF},
    };
    for (auto& t : tests) {
        double jitter = (double)(int64_t)t.draw * 5.0 * ra2::rmg::kInvUintMax;
        double raw = jitter + t.dist;
        float f_chop = ra2::rmg::ChopFloat(raw);
        float f_x87 = ra2::rmg::X87SpreadKey(t.dist, t.draw);
        printf("dist=%.6g draw=%u raw=%.17g chop=%.9g(0x%08X) x87=%.9g(0x%08X) %s\n",
               t.dist, t.draw, raw,
               (double)f_chop, *(unsigned*)&f_chop,
               (double)f_x87, *(unsigned*)&f_x87,
               f_chop == f_x87 ? "SAME" : "DIFF!");
    }
    return 0;
}
