#include "../crt_malloc.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 记录桩 core（0x7C9442）的实参序列
std::vector<std::pair<std::size_t, std::uintptr_t>>* g_args = nullptr;
std::uintptr_t g_inject = 0;

void* StubCore(std::size_t size, std::uintptr_t flag) {
    if (g_args) g_args->push_back({size, flag});
    return reinterpret_cast<void*>(g_inject);
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE size=<hex> flag=<hex> inj=<hex> -> ret <eax(hex)> core_args=<sizearg,flagarg>
struct GCase {
    std::uint32_t size = 0;
    std::uint32_t flag = 0;
    std::uint32_t inj = 0;
    std::uint32_t ret = 0;
    std::uint32_t core_size = 0;
    std::uint32_t core_flag = 0;
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
        // toks[1]="size=<hex>" toks[2]="flag=<hex>" toks[3]="inj=<hex>"
        // toks[4]="->" toks[5]="ret" toks[6]=ret toks[7]="core_args=<a,b>"
        c.size = static_cast<std::uint32_t>(
            std::stoull(toks[1].substr(5), nullptr, 16));
        c.flag = static_cast<std::uint32_t>(
            std::stoull(toks[2].substr(5), nullptr, 16));
        c.inj = static_cast<std::uint32_t>(
            std::stoull(toks[3].substr(4), nullptr, 16));
        c.ret = static_cast<std::uint32_t>(
            std::stoull(toks[6], nullptr, 16));
        const std::string ca = toks[7].substr(10);  // "size,flag"
        const auto comma = ca.find(',');
        c.core_size = static_cast<std::uint32_t>(
            std::stoull(ca.substr(0, comma), nullptr, 16));
        c.core_flag = static_cast<std::uint32_t>(
            std::stoull(ca.substr(comma + 1), nullptr, 16));
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtMallocTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c9430_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：返回值透传 core + core 实参 == (size, 全局 flag)
TEST_F(CrtMallocTest, GoldenReturnAndArgsMatch) {
    for (const GCase& c : cases_) {
        ra2::runtime::CrtMallocWorld w{};
        w.core = StubCore;
        w.flag = c.flag;  // [0xB782C4] 注入值
        std::vector<std::pair<std::size_t, std::uintptr_t>> args;
        g_args = &args;
        g_inject = c.inj;
        const void* got = ra2::runtime::malloc_legacy(&w, c.size);
        g_args = nullptr;
        EXPECT_EQ(static_cast<std::uint32_t>(
                      reinterpret_cast<std::uintptr_t>(got)),
                  c.ret)
            << "case size=" << std::hex << c.size << " flag=" << c.flag;
        ASSERT_EQ(args.size(), 1u)
            << "case size=" << std::hex << c.size;
        // 参数顺序：core(size, 全局值)——push 全局先压、push size 后压
        EXPECT_EQ(args[0].first, c.core_size);
        EXPECT_EQ(args[0].second, c.core_flag);
    }
}

// 语义核心：返回值 = core 返回值透传（包装不改）
TEST_F(CrtMallocTest, ReturnPassthrough) {
    ra2::runtime::CrtMallocWorld w{};
    w.core = StubCore;
    w.flag = 0x41;
    g_inject = 0x1234;
    EXPECT_EQ(ra2::runtime::malloc_legacy(&w, 0x100),
              reinterpret_cast<void*>(0x1234));
    g_inject = 0;  // core 返回 NULL 也原样透传（包装不查）
    EXPECT_EQ(ra2::runtime::malloc_legacy(&w, 0x100), nullptr);
}

// 全路径必然调用一次 core（size 任意——无 NULL 短路）
TEST_F(CrtMallocTest, AlwaysCallsCoreOnce) {
    ra2::runtime::CrtMallocWorld w{};
    w.core = StubCore;
    w.flag = 0x41;
    for (std::size_t sz : {std::size_t{0}, std::size_t{1}, std::size_t{0x20},
                           std::size_t{0xFFFFFFE0},
                           std::size_t{std::size_t(-1)}}) {
        std::vector<std::pair<std::size_t, std::uintptr_t>> args;
        g_args = &args;
        g_inject = 0x41;
        (void)ra2::runtime::malloc_legacy(&w, sz);
        g_args = nullptr;
        ASSERT_EQ(args.size(), 1u) << "size=" << std::hex << sz;
        EXPECT_EQ(args[0].first, sz);
        EXPECT_EQ(args[0].second, 0x41u);
    }
}