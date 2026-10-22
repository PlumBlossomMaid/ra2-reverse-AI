#include "../crt_boolnorm.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 记录桩 inner（0x7C970C）的实参序列
std::vector<std::uint32_t>* g_args = nullptr;
std::uint32_t g_inject = 0;

std::uint32_t StubInner(std::uintptr_t p) {
    if (g_args) g_args->push_back(static_cast<std::uint32_t>(p));
    return g_inject;
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE p=<hex> inj=<hex> -> ret <eax(hex)> p_arg=<hex>
struct GCase {
    std::uint32_t p = 0;
    std::uint32_t inj = 0;
    std::uint32_t ret = 0;
    std::uint32_t p_arg = 0;
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
        // toks[1]="p=<hex>" toks[2]="inj=<hex>" toks[3]="->" toks[4]="ret"
        // toks[5]=eax toks[6]="p_arg=<hex>"
        c.p = static_cast<std::uint32_t>(
            std::stoull(toks[1].substr(2), nullptr, 16));
        c.inj = static_cast<std::uint32_t>(
            std::stoull(toks[2].substr(4), nullptr, 16));
        c.ret = static_cast<std::uint32_t>(
            std::stoull(toks[5], nullptr, 16));
        c.p_arg = static_cast<std::uint32_t>(
            std::stoull(toks[6].substr(6), nullptr, 16));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtBoolNormTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c978a_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回值与 golden ret 一致 + inner 实参==p（p 被原样转发）
TEST_F(CrtBoolNormTest, GoldenReturnMatches) {
    for (const GCase& c : cases_) {
        ra2::crt::CrtBoolNormWorld w{};
        w.inner = StubInner;
        std::vector<std::uint32_t> args;
        g_args = &args;
        g_inject = c.inj;
        const std::int32_t got =
            ra2::crt::bool_normalize_legacy(&w, c.p);
        g_args = nullptr;
        EXPECT_EQ(static_cast<std::uint32_t>(got), c.ret)
            << "case p=" << std::hex << c.p << " inj=" << c.inj;
        ASSERT_EQ(args.size(), 1u);
        EXPECT_EQ(args[0], c.p_arg);  // p 原样转发（含 p==0 用例）
    }
}

// 语义核心：inner 非零 → 0；inner 为零 → -1
TEST_F(CrtBoolNormTest, Semantics) {
    ra2::crt::CrtBoolNormWorld w{};
    w.inner = StubInner;
    g_inject = 1;
    EXPECT_EQ(ra2::crt::bool_normalize_legacy(&w, 0x1000), 0);
    g_inject = 0xFFFFFFFF;
    EXPECT_EQ(ra2::crt::bool_normalize_legacy(&w, 0x1000), 0);
    g_inject = 0;
    EXPECT_EQ(ra2::crt::bool_normalize_legacy(&w, 0x1000), -1);
    g_inject = 0x80000000;  // 边界：CF 由 NEG 对非零置位
    EXPECT_EQ(ra2::crt::bool_normalize_legacy(&w, 0x1000), 0);
}

// 全路径必然调用一次 inner（含 p==0——没有 NULL 短路）
TEST_F(CrtBoolNormTest, AlwaysCallsInnerOnce) {
    ra2::crt::CrtBoolNormWorld w{};
    w.inner = StubInner;
    for (std::uint32_t p : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
        std::vector<std::uint32_t> args;
        g_args = &args;
        g_inject = 0x41;
        (void)ra2::crt::bool_normalize_legacy(&w, p);
        g_args = nullptr;
        ASSERT_EQ(args.size(), 1u) << "p=" << std::hex << p;
        EXPECT_EQ(args[0], p);
    }
}