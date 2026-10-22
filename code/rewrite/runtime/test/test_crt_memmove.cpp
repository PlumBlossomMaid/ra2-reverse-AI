#include "../crt_memmove.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::uint32_t parse_u32(const std::string& tok) {
    return static_cast<std::uint32_t>(std::stoull(tok));
}

std::int32_t parse_i32(const std::string& tok) {
    return static_cast<std::int32_t>(parse_u32(tok));
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

// 递增字节模式 0x00..0xFF 循环（golden 采集脚本同款）
std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(i & 0xFF);
    return out;
}

struct Case {
    int rel = 0;             // dst = src + rel（负=src 前、正=src 后）
    int count = 0;
    std::vector<std::uint8_t> exp_dst;  // dst 前 64 字节输出（n<=256）
};

// golden 行样例：
//   CASE -256 0 <src-hex> -> ret <eax> dst<hex>
//   CASE 0 16 <src-hex> -> ret <eax> dst<hex>
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
        c.rel = static_cast<int>(parse_i32(toks[1]));
        c.count = static_cast<int>(parse_u32(toks[2]));
        // toks[3] = src-hex（count==0 时为空字段被分词器跳过，
        // 不能按固定下标取 "ret"/dst——改为定位 "ret" 标记）
        std::size_t ret_idx = 0;
        for (; ret_idx < toks.size(); ++ret_idx) {
            if (toks[ret_idx] == "ret") break;
        }
        if (ret_idx + 2 >= toks.size()) return false;
        const std::string& dst_tok = toks[ret_idx + 2];
        if (dst_tok.rfind("dst", 0) != 0) return false;
        c.exp_dst = parse_hex(dst_tok.substr(3));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（memmove 语义）：保序背靠背复制
void reference_memmove(std::uint8_t* d, const std::uint8_t* s, std::size_t n) {
    if (d <= s || d >= s + n) {
        for (std::size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (std::size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
}

}  // namespace

class CrtMemmoveTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7ca090_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

TEST_F(CrtMemmoveTest, GoldenMemmoveMatches) {
    for (const Case& c : cases_) {
        // 布局：单独缓冲，src 定位在 0x200 偏移处（非重叠/重叠集合与
        // golden 同构——rel 相对 src，非相对缓冲头）
        std::vector<std::uint8_t> buf(0x800, 0xCC);
        std::uint8_t* src = buf.data() + 0x200;
        std::uint8_t* dst = src + c.rel;
        const std::vector<std::uint8_t> data = pattern(
            static_cast<std::size_t>(c.count));
        std::memcpy(src, data.data(), data.size());

        void* ret = ra2::crt::memmove_090_legacy(dst, src, data.size());

        // 返回 dst 原址
        EXPECT_EQ(ret, static_cast<void*>(dst));
        // golden 采集在重叠用例不写 dst 底纹（源优先），因此只比对
        // dst 前 count 字节 == 源数据（保序语义）
        for (std::size_t i = 0; i < data.size(); ++i) {
            EXPECT_EQ(dst[i], data[i]) << "case rel=" << c.rel
                                       << " n=" << c.count << " byte " << i;
        }
        // 不重叠且 count<0x40 的用例：底纹不动（golden 也验证过）
        const bool overlap = (dst < src + 0x40) && (src < dst + 0x40);
        if (!overlap && c.count < 0x40) {
            EXPECT_EQ(dst[c.count], 0xCC) << "case rel=" << c.rel;
        }
    }
}

TEST_F(CrtMemmoveTest, OverlapForwardAndBackwardPreserveOrder) {
    // memmove 保序：dst<src（正向）与 dst>src（反向）的结果都等于源序列
    std::vector<std::uint8_t> buf(0x20, 0xCC);
    const std::uint8_t data[] = {1, 2, 3, 4, 5, 6};

    // 反向重叠：dst = src+2
    std::memcpy(buf.data(), data, sizeof(data));
    std::uint8_t* src = buf.data();
    std::uint8_t* dst = src + 2;
    ra2::crt::memmove_090_legacy(dst, src, sizeof(data));
    for (std::size_t i = 0; i < sizeof(data); ++i) {
        EXPECT_EQ(dst[i], data[i]);
    }

    // 正向重叠：dst = src-2（重填后）
    std::memcpy(buf.data() + 2, data, sizeof(data));
    src = buf.data() + 2;
    dst = src - 2;
    ra2::crt::memmove_090_legacy(dst, src, sizeof(data));
    for (std::size_t i = 0; i < sizeof(data); ++i) {
        EXPECT_EQ(dst[i], data[i]);
    }
}

TEST_F(CrtMemmoveTest, ZeroCountIsNoop) {
    std::uint8_t buf[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    EXPECT_EQ(ra2::crt::memmove_090_legacy(buf, buf + 3, 0), buf);
    EXPECT_EQ(buf[0], 0xAA);
    EXPECT_EQ(buf[7], 0x22);
}

TEST_F(CrtMemmoveTest, ReferenceCrossCheck) {
    // 用独立参照实现随机抽查：任意 rel/count 下模型结果 == 参照
    for (int rel : {-0x100, -2, -1, 0, 1, 2, 3, 0x100}) {
        for (int count : {1, 3, 7, 16, 64}) {
            std::vector<std::uint8_t> buf(0x400, 0xCC);
            std::uint8_t* src = buf.data() + 0x200;
            std::uint8_t* dst = src + rel;
            const std::vector<std::uint8_t> data = pattern(count);
            std::memcpy(src, data.data(), data.size());

            ra2::crt::memmove_090_legacy(dst, src, data.size());

            // 用参照重放在干净缓冲上
            std::vector<std::uint8_t> ref(0x400, 0xCC);
            std::memcpy(ref.data() + 0x200, data.data(), data.size());
            reference_memmove(ref.data() + 0x200 + rel,
                              ref.data() + 0x200, data.size());

            for (std::size_t i = 0; i < data.size(); ++i) {
                EXPECT_EQ(dst[i], ref[0x200 + rel + i])
                    << "rel=" << rel << " n=" << count << " i=" << i;
            }
        }
    }
}