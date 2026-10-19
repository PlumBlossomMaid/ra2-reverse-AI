#include "../crt_strchr.h"

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

// golden 行：CASE s=<hex> ch=<hex> -> ret=<off|-1>
struct GCase {
    std::vector<std::uint8_t> s;
    int ch = 0;
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
        c.s = parse_hex(toks[1].substr(2));
        c.ch = static_cast<int>(std::stoul(toks[2].substr(3), nullptr, 16));
        c.ret = std::stoi(toks[4].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 strchr 语义，含 ch==0 → 终止 NUL 指针）
const char* ref_strchr(const char* s, int ch) {
    const std::uint8_t target = static_cast<std::uint8_t>(ch);
    for (;;) {
        const std::uint8_t c = static_cast<std::uint8_t>(*s);
        if (c == target) return s;
        if (c == 0) return nullptr;
        ++s;
    }
}

}  // namespace

class CrtStrchrTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7caf30_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回偏移与 golden 一致（-1=未找到返 0）
TEST_F(CrtStrchrTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        // 缓冲留足：四字节 magic 扫描可读越过串尾至对齐边界，
        // 与 golden 侧 0xCC 哨兵同口径
        std::vector<char> buf(c.s.size() + 16, static_cast<char>(0xCC));
        for (std::size_t i = 0; i < c.s.size(); ++i) {
            buf[i] = static_cast<char>(c.s[i]);
        }
        const char* s = buf.data();
        const char* ret = ra2::runtime::strchr_legacy(s, c.ch);
        const int off = (ret == nullptr)
                            ? -1
                            : static_cast<int>(ret - s);
        EXPECT_EQ(off, c.ret) << "case s.size=" << c.s.size()
                              << " ch=0x" << std::hex << c.ch;
    }
}

// 边界：ch==0 返回终止 NUL / 空串两向 / 未找到返 nullptr
TEST_F(CrtStrchrTest, ZeroCharEdgeCases) {
    char buf[16];
    std::memset(buf, 0xCC, sizeof(buf));
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = 'c';
    buf[3] = 0;
    EXPECT_EQ(ra2::runtime::strchr_legacy(buf, 0), buf + 3);
    EXPECT_EQ(ra2::runtime::strchr_legacy(buf, 'c'), buf + 2);
    EXPECT_EQ(ra2::runtime::strchr_legacy(buf, 'z'), nullptr);
    EXPECT_EQ(ra2::runtime::strchr_legacy(buf, 'a'), buf);
    char empty[8];
    std::memset(empty, 0xCC, sizeof(empty));
    empty[0] = 0;
    EXPECT_EQ(ra2::runtime::strchr_legacy(empty, 0), empty);
    EXPECT_EQ(ra2::runtime::strchr_legacy(empty, 'a'), nullptr);
}

// 参照抽查：随机串对 × 随机 ch 与独立参照实现交叉对拍
TEST_F(CrtStrchrTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CAF30u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 64; ++t) {
        const int len = static_cast<int>(rnd() % 24);
        std::vector<char> buf(static_cast<std::size_t>(len) + 16,
                              static_cast<char>(0xCC));
        for (int i = 0; i < len; ++i) {
            char c = static_cast<char>(rnd() & 0x7F);
            if (c == 0) c = 'x';
            buf[i] = c;
        }
        buf[len] = 0;
        const int ch = static_cast<int>(rnd() & 0xFF);
        const char* s = buf.data();
        const char* got = ra2::runtime::strchr_legacy(s, ch);
        const char* ref = ref_strchr(s, ch);
        EXPECT_EQ(got, ref) << "pair " << t << " len=" << len
                            << " ch=0x" << std::hex << ch;
    }
}