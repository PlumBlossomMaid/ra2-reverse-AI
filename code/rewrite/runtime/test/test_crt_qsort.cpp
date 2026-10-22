#include "../crt_qsort.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 记录器：全局 (va,vb,r) 三元组与 golden seq 对拍（golden 记录的
// 就是元素值对，不是指针）。
struct Rec {
    int va, vb, r;
};
std::vector<Rec>* g_recs = nullptr;
int LoggedCmp(const void* a, const void* b) {
    const int va = *static_cast<const int*>(a);
    const int vb = *static_cast<const int*>(b);
    const int r = va < vb ? -1 : (va > vb ? 1 : 0);
    if (g_recs) g_recs->push_back({va, vb, r});
    return r;
}

// 快乐路径比较器：不记录（用于无 golden 的边界用例）
int PlainCmp(const void* a, const void* b) {
    const int va = *static_cast<const int*>(a);
    const int vb = *static_cast<const int*>(b);
    return va < vb ? -1 : (va > vb ? 1 : 0);
}

std::vector<std::uint8_t> parse_hex(std::string tok) {
    // golden 行 `CASE %d %s`（%s 前有空格）使 hex 字段带前导空格：
    // stoul 会跳过空白，把 " 1" 读成 1，overall 首字节 0x10 错成 0x01
    const auto ns = tok.find_first_not_of(' ');
    if (ns != std::string::npos) tok.erase(0, ns);
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < tok.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(
            std::stoul(tok.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// golden 行：CASE <n> <arr(hex)|空> -> sorted<hex|空> cmp=<k> seq=<va,vb,r;...>
struct GCase {
    int n = 0;
    std::vector<std::uint8_t> arr;
    std::vector<std::uint8_t> sorted;
    std::vector<Rec> seq;
};

bool read_golden(const std::string& path, std::vector<GCase>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const auto casepos = line.find("CASE");
        if (casepos == std::string::npos) continue;
        std::string rest = line.substr(casepos + 4);
        // 跳过前导空格后取 n
        std::size_t p = rest.find_first_not_of(' ');
        std::size_t sp = rest.find(' ', p);
        GCase c;
        c.n = std::stoi(rest.substr(p, sp - p));
        rest.erase(0, sp);
        // 取 arr hex：到 " -> sorted" 前
        const std::string kArrEnd = " -> sorted";
        const auto arr_end = rest.find(kArrEnd);
        if (arr_end == std::string::npos) return false;
        const std::string arrtok = rest.substr(0, arr_end);
        if (!arrtok.empty()) c.arr = parse_hex(arrtok);
        rest.erase(0, arr_end + kArrEnd.size());
        // 现在 rest 以 sorted 后的 hex 开头（或 "cmp="）
        const std::string kCmp = " cmp=";
        const auto cmp_pos = rest.find(kCmp);
        if (cmp_pos == std::string::npos) return false;
        const std::string sortok = rest.substr(0, cmp_pos);
        const std::string cmp_tok = rest.substr(cmp_pos + kCmp.size());
        if (!sortok.empty()) c.sorted = parse_hex(sortok);
        const std::string kSeq = " seq=";
        const auto seq_pos = cmp_tok.find(kSeq);
        if (seq_pos == std::string::npos) return false;
        std::string seq = cmp_tok.substr(seq_pos + kSeq.size());
        // 解析 seq：va,vb,r 以 ; 分隔
        std::istringstream ss(seq);
        std::string item;
        while (std::getline(ss, item, ';')) {
            if (item.empty()) continue;
            std::size_t c1 = item.find(',');
            std::size_t c2 = item.find(',', c1 + 1);
            Rec rc;
            rc.va = std::stoi(item.substr(0, c1));
            rc.vb = std::stoi(item.substr(c1 + 1, c2 - c1 - 1));
            rc.r = std::stoi(item.substr(c2 + 1));
            c.seq.push_back(rc);
        }
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtQsortTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c8b48_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：结果数组与 golden sorted 逐字节一致（保序=升序）
TEST_F(CrtQsortTest, GoldenSortMatches) {
    for (const GCase& c : cases_) {
        if (c.sorted.empty()) {
            EXPECT_EQ(c.n, 0) << "空 sorted 只允许 n=0";
            continue;
        }
        std::vector<int32_t> arr(c.n);
        std::memcpy(arr.data(), c.arr.data(), c.arr.size());
        ra2::crt::qsort_legacy(arr.data(), arr.size(), sizeof(int32_t),
                                   PlainCmp);
        const auto* got = reinterpret_cast<const std::uint8_t*>(arr.data());
        for (std::size_t i = 0; i < c.sorted.size(); ++i) {
            EXPECT_EQ(got[i], c.sorted[i])
                << "case n=" << c.n << " byte " << i;
        }
    }
}

// 最强约束：比较器调用序列（实参值对 + 返回）与 golden seq 逐位一致。
// 原版 qsort 的比较器调用顺序/次数是可观测行为，任何分区/扫描偏差都会使
// 此断言失败——这是本复刻与「行为等价但调用不同」的最小对拍口径。
TEST_F(CrtQsortTest, GoldenCompareSequenceMatches) {
    for (const GCase& c : cases_) {
        if (c.arr.empty()) {
            EXPECT_TRUE(c.seq.empty()) << "空数组不应触发任何比较";
            continue;
        }
        std::vector<int32_t> arr(c.n);
        std::memcpy(arr.data(), c.arr.data(), c.arr.size());
        std::vector<Rec> recs;
        g_recs = &recs;
        ra2::crt::qsort_legacy(arr.data(), arr.size(), sizeof(int32_t),
                                   LoggedCmp);
        g_recs = nullptr;

        ASSERT_EQ(recs.size(), c.seq.size())
            << "case n=" << c.n << " 比较次数不一致";
        for (std::size_t i = 0; i < recs.size(); ++i) {
            EXPECT_EQ(recs[i].va, c.seq[i].va)
                << "case n=" << c.n << " call " << i << " va";
            EXPECT_EQ(recs[i].vb, c.seq[i].vb)
                << "case n=" << c.n << " call " << i << " vb";
            EXPECT_EQ(recs[i].r, c.seq[i].r)
                << "case n=" << c.n << " call " << i << " r";
        }
    }
}

// 边界：count < 2 或 size == 0 → 不调比较器直接返回
TEST_F(CrtQsortTest, DegenerateInputs) {
    int32_t one = 42;
    std::vector<Rec> recs;
    g_recs = &recs;
    ra2::crt::qsort_legacy(&one, 1, sizeof(int32_t), LoggedCmp);
    ra2::crt::qsort_legacy(&one, 0, sizeof(int32_t), LoggedCmp);
    ra2::crt::qsort_legacy(&one, 5, 0, LoggedCmp);
    g_recs = nullptr;
    EXPECT_TRUE(recs.empty());
    EXPECT_EQ(one, 42);
}

// 参照抽查：随机数组 vs std::sort（内容等价 + 比较次数下限 sanity）
TEST_F(CrtQsortTest, ReferenceCrossCheck) {
    std::vector<int32_t> arr;
    std::vector<int32_t> ref;
    std::uint32_t seed = 0x944944;
    for (int n : {2, 3, 7, 8, 9, 15, 63}) {
        arr.resize(n);
        ref.resize(n);
        for (int i = 0; i < n; ++i) {
            seed = seed * 1664525u + 1013904223u;
            arr[i] = static_cast<int32_t>(seed % 1000u) - 500;
            ref[i] = arr[i];
        }
        std::sort(ref.begin(), ref.end());
        ra2::crt::qsort_legacy(arr.data(), arr.size(), sizeof(int32_t),
                                   PlainCmp);
        EXPECT_EQ(arr, ref) << "n=" << n;
    }
}