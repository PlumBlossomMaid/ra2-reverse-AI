#include "../crt_strstr.h"

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

// golden 行：CASE str=<hex> sub=<hex> -> ret=<off|-1>
struct GCase {
    std::vector<std::uint8_t> str;
    std::vector<std::uint8_t> sub;
    int ret = -1;
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
        c.str = parse_hex(toks[1].substr(4));
        c.sub = parse_hex(toks[2].substr(4));
        c.ret = std::stoi(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 strstr 语义）
const char* ref_strstr(const char* hay, const char* needle) {
    if (*needle == 0) return hay;
    for (const char* p = hay; *p != 0; ++p) {
        const char* h = p;
        const char* n = needle;
        while (*h != 0 && *n != 0 && *h == *n) {
            ++h;
            ++n;
        }
        if (*n == 0) return p;
    }
    return nullptr;
}

}  // namespace

class CrtStrstrTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca4b0_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回偏移与 golden 一致（-1=未找到返 0；str/sub 含尾 NUL）
TEST_F(CrtStrstrTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const char* str =
            reinterpret_cast<const char*>(c.str.data());
        const char* sub =
            reinterpret_cast<const char*>(c.sub.data());
        const char* ret = ra2::runtime::strstr_legacy(str, sub);
        const int off = (ret == nullptr)
                            ? -1
                            : static_cast<int>(ret - str);
        EXPECT_EQ(off, c.ret) << "str.size=" << c.str.size()
                              << " sub.size=" << c.sub.size();
    }
}

// 边界：空子串返 Str / 单字符首尾 / 全串 / 未找到
TEST_F(CrtStrstrTest, BoundaryCases) {
    const char s[] = "abcdef";
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, ""), s);
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, "a"), s);
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, "f"), s + 5);
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, "cde"), s + 2);
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, "xyz"), nullptr);
    EXPECT_EQ(ra2::runtime::strstr_legacy(s, "defgh"), nullptr);
}

// 参照抽查：随机串对 × 随机子串与独立参照实现交叉对拍
TEST_F(CrtStrstrTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CA4B0u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 96; ++t) {
        const int len = static_cast<int>(rnd() % 20);
        std::vector<char> str(static_cast<std::size_t>(len) + 1);
        for (int i = 0; i < len; ++i) {
            char c = static_cast<char>(rnd() % 26 + 0x61);  // a-z
            str[i] = c;
        }
        str[len] = 0;
        // 子串：随机从 str 截一段，或随机独立串，或空
        const int mode = static_cast<int>(rnd() % 3);
        std::vector<char> sub;
        if (mode == 0 && len > 0) {
            const int off = static_cast<int>(rnd() % len);
            const int slen = static_cast<int>(rnd() % (len - off));
            sub.assign(str.begin() + off, str.begin() + off + slen);
        } else {
            const int slen = static_cast<int>(rnd() % 6);
            for (int i = 0; i < slen; ++i) {
                sub.push_back(static_cast<char>(rnd() % 26 + 0x61));
            }
        }
        sub.push_back(0);
        const char* got = ra2::runtime::strstr_legacy(str.data(), sub.data());
        const char* ref = ref_strstr(str.data(), sub.data());
        EXPECT_EQ(got, ref) << "pair " << t << " len=" << len
                            << " sub.size=" << sub.size();
    }
}