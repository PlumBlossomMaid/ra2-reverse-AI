#include "../crt_wcsncmp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// u16 小端 hex（每字符 2B，低位在前）→ uint16_t 序列
std::vector<std::uint16_t> parse_hex16(std::string tok) {
    const auto ns = tok.find_first_not_of(' ');
    if (ns != std::string::npos) tok.erase(0, ns);
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i + 3 < tok.size(); i += 4) {
        const std::uint8_t lo = static_cast<std::uint8_t>(
            std::stoul(tok.substr(i, 2), nullptr, 16));
        const std::uint8_t hi = static_cast<std::uint8_t>(
            std::stoul(tok.substr(i + 2, 2), nullptr, 16));
        out.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
    }
    return out;
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE s1=<hex16> s2=<hex16> count=<n> -> ret=<diff>
struct GCase {
    std::vector<std::uint16_t> s1;
    std::vector<std::uint16_t> s2;
    std::size_t count = 0;
    std::int32_t ret = 0;  // 无符号差值（32 位）
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
        c.s1 = parse_hex16(toks[1].substr(3));
        c.s2 = parse_hex16(toks[2].substr(3));
        c.count = std::stoul(toks[3].substr(6));
        c.ret = static_cast<std::int32_t>(std::stoul(toks[5].substr(4)));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 _wcsncmp 差值语义，但保留原版怪癖：Count 先减的
// 收尾比较 + NUL 提前终止返差——与原版一致；对拍用此参照与原版核对，
// 模型另与 golden 核对）
int ref_wcsncmp(const std::uint16_t* a, const std::uint16_t* b,
                std::size_t n) {
    if (n == 0) return 0;
    --n;
    for (;;) {
        if (a[0] == 0 || a[0] != b[0]) {
            return static_cast<int>(a[0]) - static_cast<int>(b[0]);
        }
        ++a;
        ++b;
        if (n-- == 0) {
            return static_cast<int>(a[0]) - static_cast<int>(b[0]);
        }
    }
}

}  // namespace

class CrtWcsncmpTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7cb4cc_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：32 位差值（符号化）与 golden 一致（s1/s2 含尾 0）
TEST_F(CrtWcsncmpTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const std::int32_t got =
            ra2::runtime::wcsncmp_legacy(c.s1.data(), c.s2.data(), c.count);
        EXPECT_EQ(got, c.ret) << "count=" << c.count
                              << " s1.size=" << c.s1.size();
    }
}

// 边界：Count==0 / 单字符 / 高位值 / NUL 提前两向 / 长串
TEST_F(CrtWcsncmpTest, BoundaryCases) {
    const std::uint16_t s_abc[] = {'a', 'b', 'c', 0};
    const std::uint16_t s_abd[] = {'a', 'b', 'd', 0};
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_abc, s_abc, 3), 0);
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_abc, s_abd, 3), -1);
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_abd, s_abc, 3), 1);
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_abc, s_abc, 0), 0);  // Count==0
    // 大小写差：'a'-'A'=32（非三态！）
    const std::uint16_t s_a[] = {'a', 0};
    const std::uint16_t s_A[] = {'A', 0};
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_a, s_A, 1), 32);
    // NUL 提前：'c'-0=99 / 0-'c'=-99
    const std::uint16_t s_ab[] = {'a', 'b', 0};
    const std::uint16_t s_abc2[] = {'a', 'b', 'c', 0};
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_abc2, s_ab, 3), 99);
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_ab, s_abc2, 3), -99);
    // 高位值（u16 全宽）
    const std::uint16_t s_hi1[] = {0x7FFF, 0};
    const std::uint16_t s_hi2[] = {0x8000, 0};
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_hi1, s_hi2, 1), -1);
    // 16 字符长大小写混合（尾字符差 32）
    const std::uint16_t s_l1[] = {'0','1','2','3','4','5','6','7','8','9',
                                  'a','b','c','d','e','f',0};
    const std::uint16_t s_l2[] = {'0','1','2','3','4','5','6','7','8','9',
                                  'A','B','C','D','E','F',0};
    EXPECT_EQ(ra2::runtime::wcsncmp_legacy(s_l1, s_l2, 16), 32);
}

// 参照抽查：随机 u16 串 × 随机 count vs 独立参照（标准差值语义）
TEST_F(CrtWcsncmpTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CB4CCu;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 64; ++t) {
        const int l1 = static_cast<int>(rnd() % 10);
        const int l2 = static_cast<int>(rnd() % 10);
        std::vector<std::uint16_t> a(
            static_cast<std::size_t>(l1) + 1);
        std::vector<std::uint16_t> b(
            static_cast<std::size_t>(l2) + 1);
        for (int i = 0; i < l1; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(rnd() % 0x100);
            if (c == 0) c = 0x61;
            a[i] = c;
        }
        for (int i = 0; i < l2; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(rnd() % 0x100);
            if (c == 0) c = 0x62;
            b[i] = c;
        }
        a[l1] = 0;
        b[l2] = 0;
        const std::size_t n = static_cast<std::size_t>(rnd() % 12);
        const std::int32_t got =
            ra2::runtime::wcsncmp_legacy(a.data(), b.data(), n);
        const std::int32_t ref = ref_wcsncmp(a.data(), b.data(), n);
        EXPECT_EQ(got, ref) << "pair " << t << " l1=" << l1
                            << " l2=" << l2 << " n=" << n;
    }
}