#include "../crt_thunk.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 记录桩 target（0x7C9B72）的实参序列
std::vector<std::uint32_t>* g_args = nullptr;
std::uint32_t g_inject = 0;

std::uint32_t StubTarget(std::uint32_t arg) {
    if (g_args) g_args->push_back(arg);
    return g_inject;
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE arg=<hex> inj=<hex> -> ret <eax(hex)> forwarded=<hex>
struct GCase {
    std::uint32_t arg = 0;
    std::uint32_t inj = 0;
    std::uint32_t ret = 0;
    std::uint32_t forwarded = 0;
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
        // toks[1]="arg=<hex>" toks[2]="inj=<hex>" toks[3]="->" toks[4]="ret"
        // toks[5]=eax toks[6]="forwarded=<hex>"
        c.arg = static_cast<std::uint32_t>(
            std::stoull(toks[1].substr(4), nullptr, 16));
        c.inj = static_cast<std::uint32_t>(
            std::stoull(toks[2].substr(4), nullptr, 16));
        c.ret = static_cast<std::uint32_t>(
            std::stoull(toks[5], nullptr, 16));
        c.forwarded = static_cast<std::uint32_t>(
            std::stoull(toks[6].substr(10), nullptr, 16));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtThunkTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c9bfd_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回值透传 + target 实参 == arg（原样转发）
TEST_F(CrtThunkTest, GoldenReturnAndForwardMatch) {
    for (const GCase& c : cases_) {
        ra2::crt::CrtThunkWorld w{};
        w.target = StubTarget;
        std::vector<std::uint32_t> args;
        g_args = &args;
        g_inject = c.inj;
        const std::uint32_t got = ra2::crt::parse_thunk_legacy(&w, c.arg);
        g_args = nullptr;
        EXPECT_EQ(got, c.ret) << "case arg=" << std::hex << c.arg
                              << " inj=" << c.inj;
        ASSERT_EQ(args.size(), 1u);
        EXPECT_EQ(args[0], c.forwarded);  // arg 原样转发（含 0/0xFFFFFFFF）
    }
}

// 语义核心：返回值 = target 返回值透传（包装不改）
TEST_F(CrtThunkTest, ReturnPassthrough) {
    ra2::crt::CrtThunkWorld w{};
    w.target = StubTarget;
    g_inject = 0x41;
    EXPECT_EQ(ra2::crt::parse_thunk_legacy(&w, 0x30), 0x41u);
    g_inject = 0xFFFFFFFF;
    EXPECT_EQ(ra2::crt::parse_thunk_legacy(&w, 0x30), 0xFFFFFFFFu);
    g_inject = 0;
    EXPECT_EQ(ra2::crt::parse_thunk_legacy(&w, 0x30), 0u);
}

// 全路径必然调用一次 target（arg 任意——无 NULL 短路）
TEST_F(CrtThunkTest, AlwaysCallsTargetOnce) {
    ra2::crt::CrtThunkWorld w{};
    w.target = StubTarget;
    for (std::uint32_t arg : {0u, 1u, 0x2Du, 0x30u, 0x41u,
                              0x7FFFFFFFu, 0xFFFFFFFFu}) {
        std::vector<std::uint32_t> args;
        g_args = &args;
        g_inject = 0x41;
        (void)ra2::crt::parse_thunk_legacy(&w, arg);
        g_args = nullptr;
        ASSERT_EQ(args.size(), 1u) << "arg=" << std::hex << arg;
        EXPECT_EQ(args[0], arg);
    }
}