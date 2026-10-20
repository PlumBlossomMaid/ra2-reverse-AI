#include "../crt_wcsncat.h"

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

std::string hx16(const std::vector<std::uint16_t>& seq) {
    std::ostringstream oss;
    for (std::uint16_t v : seq) {
        oss << (v & 0xFF ? (v & 0xFF) : 0) << " " << (v >> 8);
    }
    std::ostringstream out;
    for (std::uint16_t v : seq) {
        out << static_cast<char>('0' + ((v >> 4) & 0xF)) << static_cast<char>('0' + (v & 0xF));
    }
    return out.str();
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE dst=<hex16> src=<hex16> count=<n> -> out=<hex16> ret=<hex32>
struct GCase {
    std::vector<std::uint16_t> dst;
    std::vector<std::uint16_t> src;
    std::size_t count = 0;
    std::vector<std::uint16_t> out;
};

bool read_golden(const std::string& path, std::vector<GCase>* out_cases) {
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
        c.dst = parse_hex16(toks[1].substr(4));
        c.src = parse_hex16(toks[2].substr(4));
        c.count = std::stoul(toks[3].substr(6));
        // -> out=<hex16> ret=0x...
        std::size_t i = 4;
        for (; i < toks.size(); ++i) {
            if (toks[i] == "->") break;
        }
        c.out = parse_hex16(toks[i + 1].substr(4));
        out_cases->push_back(c);
    }
    return true;
}

}  // namespace

class CrtWcsncatTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7cb504_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：dst 区结果与 golden 一致（追加后含尾 NUL）
TEST_F(CrtWcsncatTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        std::vector<std::uint16_t> buf(c.dst.size() + c.count + 4, 0xCCCC);
        for (std::size_t i = 0; i < c.dst.size(); ++i) buf[i] = c.dst[i];
        std::uint16_t* got = ra2::runtime::wcsncat_legacy(
            buf.data(), c.src.data(), c.count);
        EXPECT_EQ(got, buf.data());  // ★返回 dst
        // 比较到 golden out 的长度
        ASSERT_GE(buf.size(), c.out.size());
        for (std::size_t i = 0; i < c.out.size(); ++i) {
            EXPECT_EQ(buf[i], c.out[i]) << "unit " << i;
        }
        EXPECT_EQ(buf[c.out.size() - 1], 0);  // 尾 NUL
    }
}

// 边界：Count==0 / 空 dst / 空 src / Count 截断 / src NUL 提前
TEST_F(CrtWcsncatTest, BoundaryCases) {
    std::vector<std::uint16_t> buf(16, 0xCCCC);
    // "ab" + "cd" count=3 → "abcd"
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = 0;
    const std::uint16_t src1[] = {'c', 'd', 0};
    std::uint16_t* r =
        ra2::runtime::wcsncat_legacy(buf.data(), src1, 3);
    EXPECT_EQ(r, buf.data());
    EXPECT_EQ(buf[0], 'a');
    EXPECT_EQ(buf[1], 'b');
    EXPECT_EQ(buf[2], 'c');
    EXPECT_EQ(buf[3], 'd');
    EXPECT_EQ(buf[4], 0);

    // Count==0：dst 不变（尾补 NUL）
    buf[0] = 'a';
    buf[1] = 0;
    ra2::runtime::wcsncat_legacy(buf.data(), src1, 0);
    EXPECT_EQ(buf[0], 'a');
    EXPECT_EQ(buf[1], 0);

    // 空 dst
    buf[0] = 0;
    ra2::runtime::wcsncat_legacy(buf.data(), src1, 2);
    EXPECT_EQ(buf[0], 'c');
    EXPECT_EQ(buf[1], 'd');
    EXPECT_EQ(buf[2], 0);

    // 空 src
    buf[0] = 'x';
    buf[1] = 0;
    const std::uint16_t src_empty[] = {0};
    ra2::runtime::wcsncat_legacy(buf.data(), src_empty, 5);
    EXPECT_EQ(buf[0], 'x');
    EXPECT_EQ(buf[1], 0);

    // src NUL 提前（count 大于 src 长）
    const std::uint16_t src_short[] = {'y', 0};
    buf[0] = 'z';
    buf[1] = 0;
    ra2::runtime::wcsncat_legacy(buf.data(), src_short, 8);
    EXPECT_EQ(buf[0], 'z');
    EXPECT_EQ(buf[1], 'y');
    EXPECT_EQ(buf[2], 0);
}

// 参照抽查：随机 u16 串 vs 独立参照实现（标准 wcsncat 语义）
TEST_F(CrtWcsncatTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CB504u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 64; ++t) {
        const int dl = static_cast<int>(rnd() % 6);   // dst 长（不含 NUL）
        const int sl = static_cast<int>(rnd() % 6);   // src 长（不含 NUL）
        std::vector<std::uint16_t> buf(
            static_cast<std::size_t>(dl) + static_cast<std::size_t>(sl) + 8,
            0xCCCC);
        for (int i = 0; i < dl; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(rnd() % 0x100);
            if (c == 0) c = 0x61;
            buf[i] = c;
        }
        buf[dl] = 0;
        std::vector<std::uint16_t> src(
            static_cast<std::size_t>(sl) + 1);
        for (int i = 0; i < sl; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(rnd() % 0x100);
            if (c == 0) c = 0x62;
            src[i] = c;
        }
        src[sl] = 0;
        const std::size_t n = static_cast<std::size_t>(rnd() % 10);

        // 参照：标准 wcsncat 语义
        std::vector<std::uint16_t> ref = buf;
        std::size_t i = 0;
        while (ref[i] != 0) ++i;
        std::size_t k = 0;
        while (k < n && src[k] != 0) {
            ref[i++] = src[k++];
        }
        ref[i] = 0;

        std::uint16_t* got =
            ra2::runtime::wcsncat_legacy(buf.data(), src.data(), n);
        EXPECT_EQ(got, buf.data());
        for (std::size_t j = 0; j < ref.size(); ++j) {
            if (ref[j] == 0) {
                EXPECT_EQ(buf[j], 0) << "pair " << t << " j=" << j;
                break;
            }
            EXPECT_EQ(buf[j], ref[j]) << "pair " << t << " j=" << j;
        }
    }
}