#include "../crt_wcscpy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// golden 由 Python rd32 打印，是无符号十进制（本批 ret 为 bump 内存地址，
// 测试不直接比对 ret 数值，改为断言"返回 dst 原址"语义）。
std::uint32_t parse_u32(const std::string& tok) {
    return static_cast<std::uint32_t>(std::stoull(tok));
}

std::vector<std::uint8_t> parse_hex(const std::string& tok) {
    std::vector<std::uint8_t> out;
    if (tok == "-") return out;
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

struct Case {
    std::vector<std::uint16_t> src;    // 源宽字符（含终止 NUL）
    std::uint32_t exp_ret = 0;         // = bump 地址（仅语义参考）
    std::vector<std::uint8_t> exp_dst; // 32 字节输出
};

// golden 行样例：
//   CASE 00480065006c006c006f0000 -> ret 1074896336 dst<64hex>
// src 为逐字 4 位 hex（little-endian 宽字符），dst 为 32 字节 hex
bool read_golden(const char* path, std::vector<Case>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto toks = tokens_of(line);
        if (toks.empty()) continue;
        if (toks[0] != "CASE") return false;
        Case c;
        const std::string& srch = toks[1];
        for (std::size_t i = 0; i + 3 < srch.size(); i += 4) {
            c.src.push_back(static_cast<std::uint16_t>(
                std::stoul(srch.substr(i, 4), nullptr, 16)));
        }
        c.exp_ret = parse_u32(toks[4]);
        const std::string& dst_tok = toks[5];
        if (dst_tok.rfind("dst", 0) != 0) return false;
        c.exp_dst = parse_hex(dst_tok.substr(3));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtWcscpyTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7ca489_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

TEST_F(CrtWcscpyTest, GoldenWcscpyMatches) {
    for (const Case& c : cases_) {
        std::vector<std::uint16_t> dst(16, 0xCCCC);
        std::uint16_t* ret = ra2::crt::wcscpy_legacy(
            dst.data(), c.src.data());

        // 返回 dst 原址（原版 eax 入口即存、永不修改）
        EXPECT_EQ(ret, dst.data());
        // 输出缓冲与 golden 逐字节一致（含 NUL 写入与底纹不动）
        ASSERT_GE(dst.size() * 2, c.exp_dst.size());
        for (std::size_t i = 0; i < c.exp_dst.size(); ++i) {
            const std::uint8_t byte =
                (i % 2 == 0) ? static_cast<std::uint8_t>(dst[i / 2] & 0xFF)
                             : static_cast<std::uint8_t>(dst[i / 2] >> 8);
            EXPECT_EQ(byte, c.exp_dst[i]) << "byte " << i;
        }
    }
}

TEST_F(CrtWcscpyTest, CopiesThroughNulIncludingNul) {
    // "读-写-推进-测试"流水线：NUL 本身也被写入 dst 后才返回
    std::uint16_t src[] = {'a', 'b', 0};
    std::uint16_t dst[8] = {};
    ra2::crt::wcscpy_legacy(dst, src);
    EXPECT_EQ(dst[0], 'a');
    EXPECT_EQ(dst[1], 'b');
    EXPECT_EQ(dst[2], 0);
}

TEST_F(CrtWcscpyTest, EmptyStringCopiesNul) {
    std::uint16_t src[] = {0};
    std::uint16_t dst[4] = {1, 2, 3, 4};
    ra2::crt::wcscpy_legacy(dst, src);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 2);  // 其余不动
}
