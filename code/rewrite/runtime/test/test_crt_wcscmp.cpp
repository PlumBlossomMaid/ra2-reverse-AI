#include "../crt_wcscmp.h"

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

// golden 行：CASE s1=<hex> s2=<hex> -> ret=<tri>
struct GCase {
    std::vector<std::uint16_t> s1;
    std::vector<std::uint16_t> s2;
    int ret = 0;
};

std::vector<std::uint16_t> to_u16(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        out.push_back(static_cast<std::uint16_t>(
            bytes[i] | (bytes[i + 1] << 8)));
    }
    return out;
}

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
        c.s1 = to_u16(parse_hex(toks[1].substr(3)));
        c.s2 = to_u16(parse_hex(toks[2].substr(3)));
        c.ret = std::stoi(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现：标准 wcscmp 语义（逐字符有符号差，先 NUL 先到者小）
int ref_wcscmp(const std::uint16_t* a, const std::uint16_t* b) {
    for (;;) {
        const int32_t d = static_cast<int32_t>(*a) - static_cast<int32_t>(*b);
        if (d != 0) return d < 0 ? -1 : 1;
        if (*a == 0) return 0;
        ++a;
        ++b;
    }
}

}  // namespace

class CrtWcscmpTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca5d3_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：与 golden 三态一致（s1/s2 均含尾 NUL，可直接 data()）
TEST_F(CrtWcscmpTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const int got = ra2::runtime::wcscmp_legacy(c.s1.data(), c.s2.data());
        EXPECT_EQ(got, c.ret)
            << "s1=" << c.s1.size() << "w s2=" << c.s2.size() << "w";
    }
}

// 边界：空串三态
TEST_F(CrtWcscmpTest, EmptyEdgeCases) {
    const std::uint16_t nul[1] = {0};
    const std::uint16_t x[2] = {0x58, 0};
    EXPECT_EQ(ra2::runtime::wcscmp_legacy(nul, nul), 0);
    EXPECT_EQ(ra2::runtime::wcscmp_legacy(nul, x), -1);
    EXPECT_EQ(ra2::runtime::wcscmp_legacy(x, nul), 1);
    EXPECT_EQ(ra2::runtime::wcscmp_legacy(x, x), 0);
}

// 参照抽查：与独立参照实现（标准 wcscmp 语义）随机串交叉对拍
TEST_F(CrtWcscmpTest, ReferenceCrossCheck) {
    // 固定 32 组伪随机串对（长度 0-16，字符含 0-0xFFFF 任意值——
    // 提前 NUL 与无符号零扩展均由真身语义决定）
    std::uint32_t seed = 0x7CA5D3u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::uint16_t>(seed & 0xFFFF);
    };
    for (int t = 0; t < 32; ++t) {
        std::vector<std::uint16_t> a;
        std::vector<std::uint16_t> b;
        const int la = static_cast<int>(rnd() % 17);
        const int lb = static_cast<int>(rnd() % 17);
        for (int i = 0; i < la; ++i) a.push_back(rnd());
        for (int i = 0; i < lb; ++i) b.push_back(rnd());
        a.push_back(0);
        b.push_back(0);
        const int got = ra2::runtime::wcscmp_legacy(a.data(), b.data());
        const int ref = ref_wcscmp(a.data(), b.data());
        EXPECT_EQ(got, ref)
            << "pair " << t << " la=" << la << " lb=" << lb;
    }
}
