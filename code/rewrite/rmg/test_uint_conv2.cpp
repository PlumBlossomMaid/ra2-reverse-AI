// 检查 MSVC 生成的 uint32→int64→double 转换指令
#include <cstdio>
#include <cstdint>

double UniformUnit(uint32_t r) {
    return static_cast<double>(static_cast<std::int64_t>(r)) * 2.3283064370807974e-10;
}

int main() {
    uint32_t r = 0x907E3A92;
    double d = UniformUnit(r);
    printf("r=0x%08X d=%.17g\n", r, d);
    // 正确值应该是 2424191634 * 2.328e-10 = 0.564...
    // 如果符号扩展，会变成 -1870775662 * 2.328e-10 = -0.436...
    return 0;
}
