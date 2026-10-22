#include "../crt_strnicmp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> parse_hex(std::string tok) {
    const auto ns = tok.find_first_not_of(' ');
    if (ns != std::string::npos) tok.erase(0, ns);
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < tok.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(
            std::stoul(tok.substr(i, 2), nullptr, 16)));
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

// golden 行：CASE s1=<hex> s2=<hex> count=<n> -> ret=<tri>
struct GCase {
    std::vector<std::uint8_t> s1;
    std::vector<std::uint8_t> s2;
    std::size_t count = 0;
    int ret = 0;
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
        c.s1 = parse_hex(toks[1].substr(3));
        c.s2 = parse_hex(toks[2].substr(3));
        c.count = std::stoul(toks[3].substr(6));
        c.ret = std::stoi(toks[5].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 _strnicmp C locale 语义：仅 'A'-'Z' 转小写后
// 逐字节无符号比较，NUL 参与终止）
int ref_strnicmp(const char* a, const char* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t ca = static_cast<std::uint8_t>(a[i]);
        std::uint8_t cb = static_cast<std::uint8_t>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
    return 0;
}

}  // namespace

class CrtStrnicmpTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7cd680_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：三态与 golden 一致（s1/s2 含尾 NUL，可直接 data()）
TEST_F(CrtStrnicmpTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const int got = ra2::crt::strnicmp_legacy(
            reinterpret_cast<const char*>(c.s1.data()),
            reinterpret_cast<const char*>(c.s2.data()), c.count);
        EXPECT_EQ(got, c.ret) << "count=" << c.count
                              << " s1.size=" << c.s1.size();
    }
}

// 边界：count==0 不读内存 / 双空 / NUL 提前 / 大小写边界
TEST_F(CrtStrnicmpTest, BoundaryCases) {
    const char a[] = "AbC";
    const char b[] = "aBc";
    EXPECT_EQ(ra2::crt::strnicmp_legacy(a, b, 0), 0);
    EXPECT_EQ(ra2::crt::strnicmp_legacy(a, b, 3), 0);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("", "", 5), 0);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("", "a", 5), -1);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("a", "", 5), 1);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("abc", "abd", 3), -1);
    // 转小写区间边界：'@'(0x40) 不转、'Z'(0x5A) 转、'`'(0x60) 不转、'z'(0x7A) 不转
    EXPECT_EQ(ra2::crt::strnicmp_legacy("@", "`", 1), -1);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("z", "{", 1), -1);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("Z", "z", 1), 0);
    EXPECT_EQ(ra2::crt::strnicmp_legacy("@Z", "@z", 2), 0);
    // 非 ASCII 字节（0x80+）原样参与无符号比较
    const char hi1[] = {(char)0xC3, 0};
    const char hi2[] = {(char)0xC4, 0};
    EXPECT_EQ(ra2::crt::strnicmp_legacy(hi1, hi2, 1), -1);
}

// 参照抽查：随机串对 × 随机 count 与独立参照实现交叉对拍
TEST_F(CrtStrnicmpTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CD680u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 64; ++t) {
        const int l1 = static_cast<int>(rnd() % 12);
        const int l2 = static_cast<int>(rnd() % 12);
        std::vector<char> a(static_cast<std::size_t>(l1) + 1);
        std::vector<char> b(static_cast<std::size_t>(l2) + 1);
        for (int i = 0; i < l1; ++i) {
            char c = static_cast<char>(rnd() % 128);
            if (c == 0) c = 'x';
            a[i] = c;
        }
        for (int i = 0; i < l2; ++i) {
            char c = static_cast<char>(rnd() % 128);
            if (c == 0) c = 'y';
            b[i] = c;
        }
        a[l1] = 0;
        b[l2] = 0;
        const std::size_t n = static_cast<std::size_t>(rnd() % 16);
        const int got = ra2::crt::strnicmp_legacy(a.data(), b.data(), n);
        const int ref = ref_strnicmp(a.data(), b.data(), n);
        EXPECT_EQ(got, ref) << "pair " << t << " l1=" << l1
                            << " l2=" << l2 << " n=" << n;
    }
}