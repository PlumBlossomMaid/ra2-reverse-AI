#include "../crt_wcschr.h"

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

std::vector<std::uint16_t> to_u16(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        out.push_back(static_cast<std::uint16_t>(
            bytes[i] | (bytes[i + 1] << 8)));
    }
    return out;
}

// golden 行：CASE s=<hex> c=<hex> -> ret=<off|-1>
struct GCase {
    std::vector<std::uint16_t> s;  // 含尾 NUL
    std::uint16_t c = 0;
    int ret = -1;  // 字节偏移，-1=未找到
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
        c.s = to_u16(parse_hex(toks[1].substr(2)));
        c.c = static_cast<std::uint16_t>(
            std::stoul(toks[2].substr(2), nullptr, 16));
        c.ret = std::stoi(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtWcschrTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca8c6_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回偏移与 golden 一致（-1=未找到返 0；s 含尾 NUL）
TEST_F(CrtWcschrTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const std::uint16_t* ret =
            ra2::runtime::wcschr_legacy(c.s.data(), c.c);
        // golden ret 为字节偏移（采集端 eax-s 字节差）；u16 下标 ×2 换算
        const int off = (ret == nullptr)
                            ? -1
                            : static_cast<int>(ret - c.s.data()) * 2;
        EXPECT_EQ(off, c.ret) << "s.size=" << c.s.size()
                              << " c=0x" << std::hex << c.c;
    }
}

// 边界：c==0 返终止 NUL / 空串 / 首尾 / 未找到
TEST_F(CrtWcschrTest, BoundaryCases) {
    const std::uint16_t s[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0};
    EXPECT_EQ(ra2::runtime::wcschr_legacy(s, 0), s + 5);   // c==0 → NUL
    EXPECT_EQ(ra2::runtime::wcschr_legacy(s, 0x41), s);    // 首
    EXPECT_EQ(ra2::runtime::wcschr_legacy(s, 0x45), s + 4);  // 尾
    EXPECT_EQ(ra2::runtime::wcschr_legacy(s, 0x46), nullptr);  // 未找到
    const std::uint16_t empty[] = {0};
    EXPECT_EQ(ra2::runtime::wcschr_legacy(empty, 0), empty);
    EXPECT_EQ(ra2::runtime::wcschr_legacy(empty, 0x41), nullptr);
}

// 参照抽查：随机 u16 串 × 随机 c 与独立参照实现交叉对拍
TEST_F(CrtWcschrTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CA8C6u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 96; ++t) {
        const int len = static_cast<int>(rnd() % 16);
        std::vector<std::uint16_t> s(static_cast<std::size_t>(len) + 1);
        for (int i = 0; i < len; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(rnd() & 0xFFFF);
            if (c == 0) c = 1;  // 非 NUL
            s[i] = c;
        }
        s[len] = 0;
        const std::uint16_t c = static_cast<std::uint16_t>(rnd() & 0xFFFF);
        const std::uint16_t* got =
            ra2::runtime::wcschr_legacy(s.data(), c);
        const std::uint16_t* ref = nullptr;
        for (int i = 0; i <= len; ++i) {
            if (s[i] == c) {
                ref = s.data() + i;
                break;
            }
        }
        EXPECT_EQ(got, ref) << "pair " << t << " len=" << len
                            << " c=0x" << std::hex << c;
    }
}