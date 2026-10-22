#include "../crt_alloca_probe.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

// golden 行：CASE size=<hex> -> esp_delta=<hex> ecx_restored=<0/1>
struct GCase {
    std::uint32_t size = 0;
    std::uint32_t esp_delta = 0;
    bool ecx_restored = false;
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
        c.size = static_cast<std::uint32_t>(
            std::stoul(toks[1].substr(5), nullptr, 16));
        c.esp_delta = static_cast<std::uint32_t>(
            std::stoul(toks[3].substr(10), nullptr, 16));
        c.ecx_restored = toks[4].substr(14) == "1";
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtAllocaProbeTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7ca650_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：esp_delta 与 golden 一致（= size）+ 探测序列以 size 唯一确定
TEST_F(CrtAllocaProbeTest, GoldenMatches) {
    const std::uint32_t caller_esp = 0x20000000u + 0x100000u - 0x3000u + 4u;
    for (const GCase& c : cases_) {
        std::vector<std::uint32_t> probes;
        const std::uint32_t new_esp = ra2::crt::alloca_probe_legacy(
            caller_esp, c.size, &probes);
        EXPECT_EQ(caller_esp - new_esp, c.esp_delta)
            << "size=0x" << std::hex << c.size;
        EXPECT_EQ(caller_esp - new_esp, c.size)
            << "size=0x" << std::hex << c.size;
        // 探测序列（汇编推导）：[base-0x1000*i, i=1..k] + [base-size]
        const std::uint32_t base = caller_esp;
        const std::uint32_t k = c.size / 0x1000u;
        EXPECT_EQ(probes.size(), k + 1) << "size=0x" << std::hex << c.size;
        for (std::uint32_t i = 0; i < k; ++i) {
            EXPECT_EQ(probes[i], base - 0x1000u * (i + 1))
                << "size=0x" << std::hex << c.size << " probe " << i;
        }
        EXPECT_EQ(probes[k], base - c.size)
            << "size=0x" << std::hex << c.size << " tail probe";
    }
}

// 边界：size 恰为页倍数时循环末次与收尾同址（重复探测）
TEST_F(CrtAllocaProbeTest, PageBoundaryTail) {
    std::vector<std::uint32_t> probes;
    ra2::crt::alloca_probe_legacy(0x1000u, 0x1000u, &probes);
    ASSERT_EQ(probes.size(), 2u);
    EXPECT_EQ(probes[0], 0x0u);   // 循环末次 [base-0x1000]
    EXPECT_EQ(probes[1], 0x0u);   // 收尾 [base-size] 同址
    ra2::crt::alloca_probe_legacy(0x3000u, 0x3000u, &probes);
    ASSERT_EQ(probes.size(), 4u);
    EXPECT_EQ(probes[0], 0x2000u);
    EXPECT_EQ(probes[1], 0x1000u);
    EXPECT_EQ(probes[2], 0x0u);
    EXPECT_EQ(probes[3], 0x0u);
}

// 边界：size==0 只探测 [base]
TEST_F(CrtAllocaProbeTest, ZeroSizeProbe) {
    std::vector<std::uint32_t> probes;
    const std::uint32_t new_esp =
        ra2::crt::alloca_probe_legacy(0x1000u, 0u, &probes);
    EXPECT_EQ(new_esp, 0x1000u);
    ASSERT_EQ(probes.size(), 1u);
    EXPECT_EQ(probes[0], 0x1000u);
}
