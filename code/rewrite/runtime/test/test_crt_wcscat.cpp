#include "../crt_wcscat.h"

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

// golden 行：CASE dst=<hex> src=<hex> -> out=<hex> ret_ptr=1
struct GCase {
    std::vector<std::uint16_t> dst;
    std::vector<std::uint16_t> src;
    std::vector<std::uint16_t> out;  // 16 槽含哨兵
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
        c.dst = to_u16(parse_hex(toks[1].substr(4)));
        c.src = to_u16(parse_hex(toks[2].substr(4)));
        c.out = to_u16(parse_hex(toks[4].substr(4)));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 wcscat 语义）
std::size_t ref_wcscat(std::uint16_t* dst, const std::uint16_t* src,
                       std::size_t cap) {
    std::size_t n = 0;
    while (n < cap && dst[n] != 0) ++n;          // 找 dst 终止 NUL
    std::size_t m = 0;
    while (n < cap) {                             // 追加 src（含 NUL）
        dst[n] = src[m];
        ++n;
        if (src[m] == 0) break;
        ++m;
    }
    return n;
}

}  // namespace

class CrtWcscatTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca45f_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：dst 前 16 槽（含哨兵）与 golden 一致 + 返回 dst 原址
TEST_F(CrtWcscatTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        std::vector<std::uint16_t> buf(16, 0xCCCC);
        for (std::size_t i = 0; i < c.dst.size(); ++i) {
            buf[i] = c.dst[i];
        }
        buf[c.dst.size()] = 0;  // dst 补尾 NUL
        // src 缓冲补尾 NUL（golden 的 src 仅本体无 NUL）
        std::vector<std::uint16_t> srcbuf(c.src.size() + 1, 0);
        for (std::size_t j = 0; j < c.src.size(); ++j) {
            srcbuf[j] = c.src[j];
        }
        const std::uint16_t* ret =
            ra2::crt::wcscat_legacy(buf.data(), srcbuf.data());
        EXPECT_EQ(ret, buf.data()) << "dst=" << c.dst.size()
                                   << " src=" << c.src.size();
        for (std::size_t i = 0; i < buf.size(); ++i) {
            const std::uint16_t expect =
                i < c.out.size() ? c.out[i] : 0xCCCC;
            EXPECT_EQ(buf[i], expect) << "wchar " << i;
        }
    }
}

// 边界：dst 空 / src 空 / 双空
TEST_F(CrtWcscatTest, EmptyEdgeCases) {
    std::vector<std::uint16_t> buf(8, 0xCCCC);
    std::vector<std::uint16_t> src(1, 0);  // 空 src（仅 NUL）
    buf[0] = 0;                            // 空 dst
    const std::uint16_t* ret =
        ra2::crt::wcscat_legacy(buf.data(), src.data());
    EXPECT_EQ(ret, buf.data());
    EXPECT_EQ(buf[0], 0);       // dst 保持空
    EXPECT_EQ(buf[1], 0xCCCC);  // 哨兵未动
    // dst 非空 + src 空：只补 NUL
    buf[0] = 0x41;
    buf[1] = 0;
    ra2::crt::wcscat_legacy(buf.data(), src.data());
    EXPECT_EQ(buf[0], 0x41);
    EXPECT_EQ(buf[1], 0);
    EXPECT_EQ(buf[2], 0xCCCC);
}

// 参照抽查：随机 dst/src 与独立参照实现交叉对拍
TEST_F(CrtWcscatTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CA45Fu;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::uint16_t>(seed & 0xFFFF);
    };
    for (int t = 0; t < 64; ++t) {
        const int l1 = static_cast<int>(rnd() % 9);
        const int l2 = static_cast<int>(rnd() % 9);
        std::vector<std::uint16_t> buf(24, 0xCCCC);
        for (int i = 0; i < l1; ++i) buf[i] = rnd();
        buf[l1] = 0;
        std::vector<std::uint16_t> src(static_cast<std::size_t>(l2) + 1);
        for (int i = 0; i < l2; ++i) {
            std::uint16_t c = rnd();
            if (c == 0) c = 1;  // 源内容非 NUL（NUL 仅终止）
            src[i] = c;
        }
        src[l2] = 0;
        const std::uint16_t* ret =
            ra2::crt::wcscat_legacy(buf.data(), src.data());
        EXPECT_EQ(ret, buf.data());
        std::vector<std::uint16_t> ref_buf(24, 0xCCCC);
        for (int i = 0; i < l1; ++i) ref_buf[i] = buf[i];
        ref_buf[l1] = 0;
        ref_wcscat(ref_buf.data(), src.data(), ref_buf.size());
        for (std::size_t k = 0; k < ref_buf.size(); ++k) {
            EXPECT_EQ(buf[k], ref_buf[k])
                << "pair " << t << " l1=" << l1 << " l2=" << l2
                << " wchar " << k;
        }
    }
}
