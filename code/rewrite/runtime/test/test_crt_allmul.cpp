#include "../crt_allmul.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::uint64_t parse_hex64(std::string tok) {
    const auto ns = tok.find_first_not_of(' ');
    if (ns != std::string::npos) tok.erase(0, ns);
    return std::stoull(tok, nullptr, 16);
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE a=<hex64> b=<hex64> -> ret=<hex64>
struct GCase {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    std::uint64_t ret = 0;
};

bool read_golden(const std::string& path, std::vector<GCase>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto toks = tokens_of(line);
        if (toks.empty()) continue;
        if (toks[0] != "CASE") return false;
        GCase c;
        c.a = parse_hex64(toks[1].substr(2));
        c.b = parse_hex64(toks[2].substr(2));
        c.ret = parse_hex64(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtAllmulTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c8ec0_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：乘积与 golden 一致（64 位无符号乘法，溢出按 mod 2^64 回绕）
TEST_F(CrtAllmulTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const std::uint64_t got = ra2::runtime::allmul_legacy(c.a, c.b);
        EXPECT_EQ(got, c.ret) << "a=" << c.a << " b=" << c.b;
    }
}

// 边界：0/1/溢出回绕/a_hi*b_hi 丢弃/符号位
TEST_F(CrtAllmulTest, BoundaryCases) {
    EXPECT_EQ(ra2::runtime::allmul_legacy(0, 0), 0);
    EXPECT_EQ(ra2::runtime::allmul_legacy(1, 1), 1);
    EXPECT_EQ(ra2::runtime::allmul_legacy(0, 0xFFFFFFFFFFFFFFFFull), 0);
    EXPECT_EQ(ra2::runtime::allmul_legacy(0xFFFFFFFFFFFFFFFFull, 0), 0);
    // 溢出回绕：(2^64-1)^2 mod 2^64 = 1
    EXPECT_EQ(ra2::runtime::allmul_legacy(0xFFFFFFFFFFFFFFFFull,
                                          0xFFFFFFFFFFFFFFFFull),
              1);
    // a_hi*b_hi 丢弃：0x100000000 * 0x100000000 = 2^64 ≡ 0
    EXPECT_EQ(ra2::runtime::allmul_legacy(0x100000000ull, 0x100000000ull),
              0);
    // 快速路径 32 位满值：(2^32-1)^2 = 0xFFFFFFFE00000001
    EXPECT_EQ(ra2::runtime::allmul_legacy(0xFFFFFFFFull, 0xFFFFFFFFull),
              0xFFFFFFFE00000001ull);
    // 符号位：2^63 * 2 = 2^64 ≡ 0
    EXPECT_EQ(ra2::runtime::allmul_legacy(0x8000000000000000ull, 2), 0);
    // 常规交叉：0x123456789 * 0xABCDEF01
    EXPECT_EQ(ra2::runtime::allmul_legacy(0x123456789ull, 0xABCDEF01ull),
              (0x123456789ull * 0xABCDEF01ull) & 0xFFFFFFFFFFFFFFFFull);
}

// 参照抽查：伪随机 uint64 对 vs 标准 64 位无符号乘法（mod 2^64）
TEST_F(CrtAllmulTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7C8EC0u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 128; ++t) {
        const std::uint64_t a =
            (static_cast<std::uint64_t>(rnd()) << 32) | rnd();
        const std::uint64_t b =
            (static_cast<std::uint64_t>(rnd()) << 32) | rnd();
        const std::uint64_t got = ra2::runtime::allmul_legacy(a, b);
        const std::uint64_t ref = a * b;  // 无符号乘法天然 mod 2^64
        EXPECT_EQ(got, ref) << "pair " << t << " a=" << a << " b=" << b;
    }
}