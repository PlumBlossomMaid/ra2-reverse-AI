#include "../crt_stricmp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// golden 由 Python 打印，全部十进制 unsigned（-1 → 4294967295），先按
// uint32_t 解析再转 int32_t（batch 936 同款坑）。
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

// 与 golden 采集桩一致：ASCII tolower（'A'..'Z' → +0x20）
class StubWorld : public ra2::runtime::CrtStricmpWorld {
 public:
    bool locale = false;
    std::vector<std::uint32_t> tolower_inputs;  // LocaleToLower 入参序列
    int acquire = 0;
    int release = 0;

    bool LocaleActive() const override { return locale; }

    std::uint32_t LocaleToLower(std::uint32_t ch) override {
        tolower_inputs.push_back(ch);
        const std::uint32_t c = ch & 0xFF;
        return (0x41 <= c && c <= 0x5A) ? (c + 0x20) : c;
    }

    void AcquireLock() override { ++acquire; }
    void ReleaseLock() override { ++release; }
};

struct Case {
    std::vector<std::uint8_t> s1;
    std::vector<std::uint8_t> s2;
    bool locale = false;
    std::int32_t exp_ret = 0;
    int exp_lockf = 0;
    int exp_locka = 0;
    std::vector<std::uint8_t> exp_tl;  // tolower 入参序列（- 为空）
};

// golden 行样例：
//   CASE 48656c6c6f 68454c4c4f 0 -> ret 0 lockf 0 locka 0 tl -
//   CASE - - 0 -> ret 0 lockf 0 locka 0 tl -
//   CASE - 61 0 -> ret 4294967295 lockf 0 locka 0 tl -
//   CASE 48656c6c6f 68454c4c4f 1 -> ret 0 lockf 0 locka 0 tl 486865456c4c6c4c6f4f
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
        c.s1 = parse_hex(toks[1]);
        c.s2 = parse_hex(toks[2]);
        c.locale = (parse_u32(toks[3]) != 0);
        // toks[4]="->" toks[5]="ret" toks[6]=ret 值
        c.exp_ret = parse_i32(toks[6]);
        // toks[7]="lockf" toks[8]=lockf toks[9]="locka" toks[10]=locka
        c.exp_lockf = static_cast<int>(parse_u32(toks[8]));
        c.exp_locka = static_cast<int>(parse_u32(toks[10]));
        // toks[11]="tl" toks[12]=序列或 '-'
        if (toks.size() > 12 && toks[12] != "-") {
            c.exp_tl = parse_hex(toks[12]);
        }
        out->push_back(c);
    }
    return true;
}

std::string bytes_to_str(const std::vector<std::uint8_t>& b) {
    return std::string(b.begin(), b.end());
}

}  // namespace

class CrtStricmpTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7c8d20_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

TEST_F(CrtStricmpTest, LocaleGlobalAddressMatchesHeader) {
    // 原版 locale 全局（==0 快路径 / !=0 慢路径）
    EXPECT_EQ(ra2::runtime::kStricmpLocaleGlobal, 0xB782A0u);
}

TEST_F(CrtStricmpTest, GoldenStricmpMatches) {
    for (const Case& c : cases_) {
        StubWorld w;
        w.locale = c.locale;
        std::string s1 = bytes_to_str(c.s1);
        std::string s2 = bytes_to_str(c.s2);

        std::int32_t ret =
            ra2::runtime::stricmp_legacy(w, s1.c_str(), s2.c_str());

        EXPECT_EQ(ret, c.exp_ret) << "s1=" << s1 << " s2=" << s2;
        // 锁平衡：慢路径 acquire/release 各一次（快路径均为 0）
        EXPECT_EQ(w.acquire, c.locale ? 1 : 0);
        EXPECT_EQ(w.release, c.locale ? 1 : 0);
        // tolower 调用序列（慢路径先 s1 后 s2）
        ASSERT_EQ(w.tolower_inputs.size(), c.exp_tl.size());
        for (std::size_t i = 0; i < c.exp_tl.size(); ++i) {
            EXPECT_EQ(w.tolower_inputs[i], c.exp_tl[i])
                << "tolower #" << i;
        }
    }
}

TEST_F(CrtStricmpTest, AsciiFoldOnlyManglesUpperCase) {
    // 快路径折叠：仅 'A'..'Z' +0x20；'@' 0x40 减 0x41 回绕仍原样
    EXPECT_EQ(ra2::runtime::stricmp_ascii_fold(0x41), 0x61);  // 'A'→'a'
    EXPECT_EQ(ra2::runtime::stricmp_ascii_fold(0x5A), 0x7A);  // 'Z'→'z'
    EXPECT_EQ(ra2::runtime::stricmp_ascii_fold(0x61), 0x61);  // 'a' 原样
    EXPECT_EQ(ra2::runtime::stricmp_ascii_fold(0x40), 0x40);  // '@' 原样
    EXPECT_EQ(ra2::runtime::stricmp_ascii_fold(0x00), 0x00);  // NUL 原样
}

TEST_F(CrtStricmpTest, SlowPathCallsTolowerInS1ThenS2Order) {
    // 慢路径调用序实证：push eax 存 s2、第一参 push ebx=s1 → 先 s1 后 s2
    StubWorld w;
    w.locale = true;
    const char* s1 = "abc";
    const char* s2 = "abD";
    ra2::runtime::stricmp_legacy(w, s1, s2);
    // 字符对：(c,c)、(c,D) 中 D 是首处不等 → 调用 (s1='D', s2='c'→ 等等，
    // 实为 s1[2]='c'、s2[2]='D' 不等 → tolower('c') 先、tolower('D') 后）
    ASSERT_EQ(w.tolower_inputs.size(), 2u);
    EXPECT_EQ(w.tolower_inputs[0], 'c');
    EXPECT_EQ(w.tolower_inputs[1], 'D');
}
