#include "../crt_wcsncpy.h"

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

// golden 行：CASE count=<n> src=<hex> -> dst=<hex> ret_ptr=1
struct GCase {
    int count = 0;
    std::vector<std::uint16_t> src;
    std::vector<std::uint16_t> dst;
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
        // toks[1]="count=<n>" toks[2]="src=<hex>" toks[3]="->" toks[4]="dst=<hex>"
        c.count = std::stoi(toks[1].substr(6));
        const auto srchex = parse_hex(toks[2].substr(4));
        const auto dsthex = parse_hex(toks[4].substr(4));
        for (std::size_t i = 0; i + 1 < srchex.size(); i += 2) {
            c.src.push_back(static_cast<std::uint16_t>(
                srchex[i] | (srchex[i + 1] << 8)));
        }
        for (std::size_t i = 0; i + 1 < dsthex.size(); i += 2) {
            c.dst.push_back(static_cast<std::uint16_t>(
                dsthex[i] | (dsthex[i + 1] << 8)));
        }
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtWcsncpyTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca422_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：dst 前 count 个宽字符与 golden 一致 + 返回 dst 原址
TEST_F(CrtWcsncpyTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        std::vector<std::uint16_t> dst(static_cast<std::size_t>(c.count) + 8,
                                       0xCCCC);
        const std::uint16_t* ret = ra2::runtime::wcsncpy_legacy(
            dst.data(), c.src.empty() ? nullptr : c.src.data(),
            static_cast<std::size_t>(c.count));
        EXPECT_EQ(ret, dst.data()) << "case count=" << c.count;
        for (std::size_t i = 0; i < dst.size(); ++i) {
            const std::uint16_t expect =
                i < c.dst.size() ? c.dst[i] : 0xCCCC;
            EXPECT_EQ(dst[i], expect)
                << "case count=" << c.count << " wchar " << i;
        }
    }
}

// 边界语义：count==0 不碰任何内存
TEST_F(CrtWcsncpyTest, ZeroCountNoop) {
    std::uint16_t dst[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    const std::uint16_t* ret =
        ra2::runtime::wcsncpy_legacy(dst, dst, 0);
    EXPECT_EQ(ret, dst);
    EXPECT_EQ(dst[0], 0x1111);
    EXPECT_EQ(dst[3], 0x4444);
}

// 参照抽查：与独立参照实现（wcsncpy 语义）随机 count/src 交叉对拍
TEST_F(CrtWcsncpyTest, ReferenceCrossCheck) {
    for (int count : {0, 1, 2, 3, 5, 8, 16}) {
        for (int srclen : {0, 1, 3, 7, 10, 16}) {
            // 源缓冲恒非空且足够长：srclen + count + 2 元素（全 0 初始化）
            // 并补 NUL 终止——wcsncpy 按 NUL 提前停止，短源合法且恒可读
            // count 个（杜绝 srclen=0 时传 nullptr 读空指针）
            std::vector<std::uint16_t> src(
                static_cast<std::size_t>(srclen) + static_cast<std::size_t>(count) + 2,
                0);
            std::uint32_t seed = 0x9CA422u + static_cast<std::uint32_t>(count * 31 + srclen);
            for (int i = 0; i < srclen; ++i) {
                seed = seed * 1664525u + 1013904223u;
                src[i] = static_cast<std::uint16_t>(seed & 0xFFFF);
            }
            src[srclen] = 0;  // NUL 终止（srclen==0 时 src[0]=0，src 仍可读）
            // 期望：复制 min(count, 首个 NUL 前长度+1)，剩余填 0
            std::vector<std::uint16_t> got(static_cast<std::size_t>(count) + 4,
                                           0xCCCC);
            std::vector<std::uint16_t> ref(static_cast<std::size_t>(count) + 4,
                                           0xCCCC);
            ra2::runtime::wcsncpy_legacy(
                got.data(), src.data(),
                static_cast<std::size_t>(count));
            // 参照实现
            std::size_t i = 0;
            for (; i < static_cast<std::size_t>(count); ++i) {
                const std::uint16_t ch =
                    i < src.size() ? src[i] : 0;
                ref[i] = ch;
                if (ch == 0) break;
            }
            for (; i < static_cast<std::size_t>(count); ++i) ref[i] = 0;
            for (std::size_t k = 0; k < ref.size(); ++k) {
                EXPECT_EQ(got[k], ref[k])
                    << "count=" << count << " srclen=" << srclen
                    << " wchar " << k;
            }
        }
    }
}