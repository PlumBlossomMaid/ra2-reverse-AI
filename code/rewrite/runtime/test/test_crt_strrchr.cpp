#include "../crt_strrchr.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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

// golden 行：CASE s=<hex> c=<hex> -> ret=<off|-1>
struct GCase {
    std::vector<std::uint8_t> s;
    int c = 0;
    int ret = 0;  // 字节偏移，-1=未找到
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
        c.s = parse_hex(toks[1].substr(2));
        c.c = static_cast<int>(std::stoul(toks[2].substr(2), nullptr, 16));
        c.ret = std::stoi(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 strrchr 语义：最后一次出现；c==0 返终止 NUL
// 指针；c 只取低 8 位）
const char* ref_strrchr(const char* s, int c) {
    const std::uint8_t target = static_cast<std::uint8_t>(c);
    const auto* p = reinterpret_cast<const std::uint8_t*>(s);
    const std::uint8_t* found = nullptr;
    for (;;) {
        if (*p == target) found = p;
        if (*p == 0) break;
        ++p;
    }
    return reinterpret_cast<const char*>(found);
}

}  // namespace

class CrtStrrchrTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c8df0_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：偏移与 golden 一致（-1=未找到返 0；0.. = 相对 s 的字节偏移）
TEST_F(CrtStrrchrTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        const char* s = reinterpret_cast<const char*>(c.s.data());
        const char* got = ra2::runtime::strrchr_legacy(s, c.c);
        if (c.ret < 0) {
            EXPECT_EQ(got, nullptr) << "c=0x" << std::hex << (c.c & 0xFF)
                                    << " s=" << c.s.size() << "B";
        } else {
            ASSERT_NE(got, nullptr);
            EXPECT_EQ(static_cast<int>(got - s), c.ret)
                << "c=0x" << std::hex << (c.c & 0xFF);
        }
    }
}

// 边界：c==0 返终止 NUL / 空串两向 / 首尾命中 / 未找到 / 高位忽略
// ★注意：MSVC 不合并相同字符串字面量（每使用点独立存储），期望值必须
//   建立在同一个串指针上，否则地址天然不同。
TEST_F(CrtStrrchrTest, BoundaryCases) {
    const char* abc = "abc";
    EXPECT_EQ(ra2::runtime::strrchr_legacy(abc, 'b'),
              std::strchr(abc, 'b'));
    EXPECT_EQ(ra2::runtime::strrchr_legacy(abc, 0), abc + 3);  // 终止 NUL
    EXPECT_EQ(ra2::runtime::strrchr_legacy(abc, 'a'), abc);    // 首命中
    EXPECT_EQ(ra2::runtime::strrchr_legacy(abc, 'c'), abc + 2);  // 尾命中
    EXPECT_EQ(ra2::runtime::strrchr_legacy(abc, 'x'), nullptr);  // 未找到

    const char* empty = "";
    EXPECT_EQ(ra2::runtime::strrchr_legacy(empty, 0), empty);  // 空串 c==0
    EXPECT_EQ(ra2::runtime::strrchr_legacy(empty, 'a'), nullptr);

    const char* single = "a";
    EXPECT_EQ(ra2::runtime::strrchr_legacy(single, 'a'), single);
    EXPECT_EQ(ra2::runtime::strrchr_legacy(single, 'a'),
              std::strchr(single, 'a'));

    const char* aba = "ababa";
    EXPECT_EQ(ra2::runtime::strrchr_legacy(aba, 'a'), aba + 4);  // 多次返最后

    // ★c 只取低 8 位：0x1FF 与 0xFF 等价
    const char* hi = "abc\xff";
    EXPECT_EQ(ra2::runtime::strrchr_legacy(hi, 0x1FF), hi + 3);
}

// 参照抽查：伪随机字节串 × 随机 c（含 0/高位）vs 独立参照实现
TEST_F(CrtStrrchrTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7C8DF0u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 96; ++t) {
        const int len = static_cast<int>(rnd() % 14);
        char buf[32];
        for (int i = 0; i < len; ++i) {
            char c = static_cast<char>(rnd() % 200);
            if (c == 0) c = 'z';
            buf[i] = c;
        }
        buf[len] = 0;
        buf[len + 1] = 0;
        const int c = static_cast<int>(rnd() % 512);  // 含高位忽略路径
        const char* got = ra2::runtime::strrchr_legacy(buf, c);
        const char* ref = ref_strrchr(buf, c);
        EXPECT_EQ(got, ref) << "pair " << t << " len=" << len
                            << " c=0x" << std::hex << (c & 0xFF);
    }
}