// 验证 ChopFloat 是否真的被调用
#include <cstdio>
#include <cmath>
#include "scaled_random.h"

int main() {
    // 测试 ChopFloat
    double test = 0.1;
    float f_near = static_cast<float>(test);
    float f_chop = ra2::rmg::ChopFloat(test);
    printf("test=%.17g\n", test);
    printf("static_cast<float> = 0x%08X (%.17g)\n",
           *(unsigned*)&f_near, (double)f_near);
    printf("ChopFloat          = 0x%08X (%.17g)\n",
           *(unsigned*)&f_chop, (double)f_chop);
    printf("diff = %d\n", f_near != f_chop ? 1 : 0);

    // 测试更多值
    int diffs = 0;
    for (int i = 1; i < 100000; ++i) {
        double x = static_cast<double>(i) / 100000.0;
        float fn = static_cast<float>(x);
        float fc = ra2::rmg::ChopFloat(x);
        if (fn != fc) ++diffs;
    }
    printf("100000 samples: %d diffs\n", diffs);
    return 0;
}
