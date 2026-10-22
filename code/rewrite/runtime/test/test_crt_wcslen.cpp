#include "../crt_wcslen.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::uint32_t parse_u32(const std::string& tok) {
    return static_cast<std::uint32_t>(std::stoull(tok));
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

struct Case {
    std::vector<std::uint16_t> src;  // 含终止 NUL
    std::uint32_t exp_ret = 0;
};

// golden 行样例：
//   CASE 0000 -> ret 0
//   CASE 006100620000 -> ret 2
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
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtWcslenTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7ca405_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

TEST_F(CrtWcslenTest, GoldenWcslenMatches) {
    for (const Case& c : cases_) {
        std::uint32_t ret = ra2::crt::wcslen_legacy(c.src.data());
        EXPECT_EQ(ret, c.exp_ret) << "src 首字 0x" << std::hex
                                  << (c.src.empty() ? 0 : c.src[0]);
    }
}

TEST_F(CrtWcslenTest, EmptyStringReturnsZero) {
    // 原版 s[0]==0 走独立分支返回 0（空串特例）
    const std::uint16_t s[] = {0};
    EXPECT_EQ(ra2::crt::wcslen_legacy(s), 0u);
}

TEST_F(CrtWcslenTest, NulIndexIsLength) {
    // "读-推-测"语义：返回第一个 NUL 的索引（= 字符串长度），
    // 0xFFFF 字符不算终止（非 0）
    const std::uint16_t s1[] = {'a', 0xFFFF, 0};
    EXPECT_EQ(ra2::crt::wcslen_legacy(s1), 2u);

    const std::uint16_t s2[] = {0xFFFF, 0};
    EXPECT_EQ(ra2::crt::wcslen_legacy(s2), 1u);
}
