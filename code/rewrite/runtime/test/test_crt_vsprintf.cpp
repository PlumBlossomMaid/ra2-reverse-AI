#include "../crt_format.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// golden 由 Python rd32 打印，是无符号十进制（-998 之类已按 uint32 展开）。
// 必须先按 uint32_t 解析再转 int32_t，否则 libstdc++ 会把超 INT32_MAX 的
// 字符串置成 INT_MAX（batch 936 踩坑）。
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

// 桩世界：按 golden 输入写 n 字节 payload 并推进槽、返回 R，同时记录
// 外壳透传给引擎的全部参数（透传语义是对拍点之一）。
class StubWorld : public ra2::crt::CrtFormatWorld {
 public:
    std::size_t n = 0;
    std::vector<std::uint8_t> payload;
    std::int32_t r = 0;

    int calls = 0;
    int term_calls = 0;
    std::uint8_t* slot_seen = nullptr;
    std::uint32_t fmt_seen = 0;
    std::uint32_t args_seen = 0;

    std::int32_t WideFormat(std::uint8_t** slot, std::uint32_t fmt,
                            std::uint32_t args) override {
        ++calls;
        slot_seen = *slot;
        fmt_seen = fmt;
        args_seen = args;
        if (n > 0) {
            for (std::size_t i = 0; i < n; ++i) (*slot)[i] = payload[i];
            *slot += n;
        }
        return r;
    }

    void WriteTerminator(std::uint8_t** slot, std::uint32_t value) override {
        ++term_calls;
    }
};

struct Case {
    int n = 0;
    std::vector<std::uint8_t> payload;
    std::int32_t r = 0;
    std::int32_t exp_ret = 0;
    std::vector<std::uint8_t> exp_dst;
    int exp_term = 0;
};

// golden 行样例：
//   WRITE 0 - r 0 -> ret 0 dst00cccc... term 0
//   WRITE 1 58 r 1 -> ret 1 dst5800cccc... term 0
//   WRITE 0 - r -1 -> ret 4294967295 dst00cccc... term 0
bool read_golden(const char* path, std::vector<Case>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto toks = tokens_of(line);
        if (toks.empty()) continue;
        if (toks[0] != "WRITE") return false;
        Case c;
        c.n = std::stoi(toks[1]);
        c.payload = parse_hex(toks[2]);
        // toks[3] = "r"，toks[4] = R
        c.r = parse_i32(toks[4]);
        // toks[5] = "->"，toks[6] = "ret"，toks[7] = ret 值
        c.exp_ret = parse_i32(toks[7]);
        const std::string& dst_tok = toks[8];
        if (dst_tok.rfind("dst", 0) != 0) return false;
        c.exp_dst = parse_hex(dst_tok.substr(3));
        // toks[9] = "term"，toks[10] = 调用次数
        c.exp_term = static_cast<int>(parse_u32(toks[10]));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtVsprintfTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7c8ef4_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

TEST_F(CrtVsprintfTest, GoldenVsprintfShellMatches) {
    for (const Case& c : cases_) {
        StubWorld w;
        w.n = static_cast<std::size_t>(c.n);
        w.payload = c.payload;
        w.r = c.r;

        std::vector<std::uint8_t> dst(16, 0xCC);  // 与 golden 采集同款底纹
        const std::uint32_t fmt = 0x401234;
        const std::uint32_t args = 0x402468;

        std::int32_t ret = ra2::crt::vsprintf_legacy(
            w, dst.data(), fmt, args);

        // 返回值原样透传（含负值）
        EXPECT_EQ(ret, c.exp_ret);
        // 引擎被调一次，fmt/args 原样透传
        EXPECT_EQ(w.calls, 1);
        EXPECT_EQ(w.fmt_seen, fmt);
        EXPECT_EQ(w.args_seen, args);
        // js 分支不可达：上限 0x7FFFFFFF 减一次仍非负
        EXPECT_EQ(w.term_calls, c.exp_term);
        EXPECT_EQ(w.term_calls, 0);
        // 输出缓冲：payload + **单个** NUL（ANSI vsprintf），其余不动
        ASSERT_GE(dst.size(), c.exp_dst.size());
        for (std::size_t i = 0; i < c.exp_dst.size(); ++i) {
            EXPECT_EQ(dst[i], c.exp_dst[i]) << "case n=" << c.n << " byte " << i;
        }
        // 引擎看到的槽起点 = dst 原址（外壳不预先移动槽）
        EXPECT_EQ(w.slot_seen, dst.data());
    }
}

TEST_F(CrtVsprintfTest, SingleNulWrittenAndSlotNotAdvanced) {
    // 单 NUL 布局：ANSI 窄字符串终止符——payload 后一个 0 字节，槽不推进
    // （0x7C8F2F and byte [eax],0 → jmp 收尾，无 inc）
    StubWorld w;
    w.n = 3;
    w.payload = {'a', 'b', 'c'};
    w.r = 3;
    std::vector<std::uint8_t> dst(16, 0xCC);
    ra2::crt::vsprintf_legacy(w, dst.data(), 0, 0);
    EXPECT_EQ(dst[0], 'a');
    EXPECT_EQ(dst[1], 'b');
    EXPECT_EQ(dst[2], 'c');
    EXPECT_EQ(dst[3], 0);       // 单 NUL
    EXPECT_EQ(dst[4], 0xCC);    // 槽未推进，后面不动
}

TEST_F(CrtVsprintfTest, EmptyOutputGetsSingleNulAtStart) {
    // n=0：槽不动，单 NUL 落在 dst 起点（vsprintf 窄版 vs vsprintfW 双 NUL）
    StubWorld w;
    w.n = 0;
    w.r = 0;
    std::vector<std::uint8_t> dst(16, 0xCC);
    ra2::crt::vsprintf_legacy(w, dst.data(), 0, 0);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 0xCC);
}

TEST_F(CrtVsprintfTest, PairsWithVsprintfwByNulCount) {
    // 窄/宽配对实证：0x7C8EF4 单 NUL vs 0x7CA564 双 NUL
    StubWorld w;
    w.n = 1;
    w.payload = {'X'};
    w.r = 1;
    std::vector<std::uint8_t> dst(16, 0xCC);
    ra2::crt::vsprintf_legacy(w, dst.data(), 0, 0);
    EXPECT_EQ(dst[0], 'X');
    EXPECT_EQ(dst[1], 0);
    EXPECT_EQ(dst[2], 0xCC);

    std::vector<std::uint8_t> dst2(16, 0xCC);
    ra2::crt::vsprintfw_legacy(w, dst2.data(), 0, 0);
    EXPECT_EQ(dst2[0], 'X');
    EXPECT_EQ(dst2[1], 0);
    EXPECT_EQ(dst2[2], 0);
    EXPECT_EQ(dst2[3], 0xCC);
}
