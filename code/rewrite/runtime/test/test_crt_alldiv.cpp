#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "crt_alldiv.h"
#include "gtest/gtest.h"

namespace {

struct GoldenCase {
    std::int64_t a;
    std::int64_t b;
    std::int64_t ret;
};

// 解析 golden 文件 "CASE a=<hex64> b=<hex64> -> ret=<hex64>"
std::vector<GoldenCase> LoadGolden() {
    std::vector<GoldenCase> out;
    std::string path = std::string(RA2_GOLDEN_DIR);
    path += "/deadcode_7c92d0_golden.txt";
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot open golden: " << path;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("CASE a=", 0) != 0) {
            continue;
        }
        std::uint64_t a = 0, b = 0, ret = 0;
        if (std::sscanf(line.c_str(),
                        "CASE a=%llx b=%llx -> ret=%llx",
                        &a, &b, &ret) == 3) {
            out.push_back({static_cast<std::int64_t>(a),
                           static_cast<std::int64_t>(b),
                           static_cast<std::int64_t>(ret)});
        }
    }
    return out;
}

}  // namespace

// 与 Unicorn 真身逐位对拍（golden 基准）
TEST(CrtAlldiv, GoldenMatches) {
    const std::vector<GoldenCase> cases = LoadGolden();
    ASSERT_FALSE(cases.empty());
    for (const GoldenCase& c : cases) {
        const std::int64_t got = ra2::crt::alldiv_legacy(c.a, c.b);
        EXPECT_EQ(got, c.ret)
            << "a=0x" << std::hex << static_cast<std::uint64_t>(c.a)
            << " b=0x" << static_cast<std::uint64_t>(c.b)
            << " got=0x" << static_cast<std::uint64_t>(got)
            << " expect=0x" << static_cast<std::uint64_t>(c.ret);
    }
}

// 边界用例（golden 之外的补充）
TEST(CrtAlldiv, BoundaryCases) {
    using ra2::crt::alldiv_legacy;
    // 除零在 golden 外不测（原版 div #DE）
    EXPECT_EQ(alldiv_legacy(0, 7), 0);
    EXPECT_EQ(alldiv_legacy(7, 7), 1);
    EXPECT_EQ(alldiv_legacy(-7, 7), -1);
    EXPECT_EQ(alldiv_legacy(7, -7), -1);
    EXPECT_EQ(alldiv_legacy(-7, -7), 1);
    // 截断向零（非 floor）
    EXPECT_EQ(alldiv_legacy(-7, 2), -3);  // -3.5 → -3
    EXPECT_EQ(alldiv_legacy(7, -2), -3);
    EXPECT_EQ(alldiv_legacy(-1, 0x7FFFFFFF), 0);
    // INT64_MAX / 1
    EXPECT_EQ(alldiv_legacy(INT64_MAX, 1), INT64_MAX);
    EXPECT_EQ(alldiv_legacy(INT64_MIN, 1), INT64_MIN);
    // INT64_MIN 绝对值化自反路径（-a 回绕）
    EXPECT_EQ(alldiv_legacy(INT64_MIN, 2), INT64_MIN / 2);
    // 商恰好跨过 int32 边界（快路径高商非零）
    EXPECT_EQ(alldiv_legacy(0x280000000LL, 3), 0x280000000LL / 3);
}

// 与标准库除法交叉抽查（截断向零语义一致）
TEST(CrtAlldiv, ReferenceCrossCheck) {
    using ra2::crt::alldiv_legacy;
    const std::int64_t pairs[][2] = {
        {123456789, 4567},
        {-123456789, 4567},
        {123456789, -4567},
        {-123456789, -4567},
        {0x123456789ABCDEF0LL, 0x100000000LL},
        {0xFEDCBA9876543210LL, 0x100000000LL},
        {1000000000000LL, 999999},
        {-1000000000000LL, 999999},
        {INT64_MAX, INT64_MAX},
        {INT64_MIN, INT64_MAX},
        {INT64_MAX, INT64_MIN},
        {7, 3},
        {-7, 3},
    };
    for (const auto& p : pairs) {
        const std::int64_t a = p[0];
        const std::int64_t b = p[1];
        // 标准库除法（截断向零，与 x86 IDIV 一致）
        const std::int64_t expect = a / b;
        // 原版 __alldiv 对 INT64_MIN / -1 会 #DE（调用方契约），跳过
        if (a == INT64_MIN && b == -1) {
            continue;
        }
        EXPECT_EQ(alldiv_legacy(a, b), expect) << a << " / " << b;
    }
}