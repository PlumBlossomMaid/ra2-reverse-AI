#include "../crt_free.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 记录 free_legacy 对外部世界（锁/查找/释放/HeapFree）的调用序列
struct WorldCall {
    std::string tag;
    long a, b, c;
    bool operator==(const WorldCall& o) const {
        return tag == o.tag && a == o.a && b == o.b && c == o.c;
    }
};

std::vector<WorldCall>* g_calls = nullptr;

void StubEnter(int which) { g_calls->push_back({"ENTER", which, 0, 0}); }
void StubLeave(int which) { g_calls->push_back({"LEAVE", which, 0, 0}); }
void* StubFind(void* p) {
    g_calls->push_back({"FIND", (long)(intptr_t)p, 0, 0});
    return p == nullptr ? nullptr : (void*)0x5678;  // 默认命中段
}
void StubFreeSeg(void* seg, void* p) {
    g_calls->push_back({"FREESEG", (long)(intptr_t)seg, (long)(intptr_t)p, 0});
}
void StubHeapFree(void* heap, unsigned flags, void* p) {
    g_calls->push_back(
        {"HEAPFREE", (long)(intptr_t)heap, (long)(intptr_t)flags, (long)(intptr_t)p});
}

// 命中/未命中切换：用全局决定 StubFind 返回值
bool* g_find_hit = nullptr;
void* g_find_seg = nullptr;  // 按 golden 用例注入的命中段
void* StubFindSwitch(void* p) {
    g_calls->push_back({"FIND", (long)(intptr_t)p, 0, 0});
    return *g_find_hit ? g_find_seg : nullptr;
}

ra2::runtime::CrtFreeWorld MakeWorld(bool* find_hit) {
    ra2::runtime::CrtFreeWorld w{};
    w.enter_lock = StubEnter;
    w.leave_lock = StubLeave;
    if (find_hit) {
        w.find_segment = StubFindSwitch;
    } else {
        w.find_segment = StubFind;
    }
    w.free_segment = StubFreeSeg;
    w.heap_free = StubHeapFree;
    w.heap_handle = (void*)0x12345678;
    return w;
}

// golden 行样例：
//   CASE NULL -> seq=
//   CASE HIT p=1000 seg=5678 -> seq=ENTER,9;FIND,4096;FREESEG,22136,4096;LEAVE,9
//   CASE MISS p=2000 -> seq=ENTER,9;FIND,8192;LEAVE,9;HEAPFREE,305419896,0,8192
struct GCase {
    std::string tag;      // NULL / HIT / MISS
    long p = 0;
    long seg = 0;         // HIT 用例期望的查找命中段（stub 注入）
    std::vector<WorldCall> seq;
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
        GCase c;
        std::size_t sp = rest.find_first_not_of(' ');
        std::size_t e1 = rest.find(' ', sp);
        c.tag = rest.substr(sp, e1 - sp);
        const auto seqtok = rest.find("seq=");
        const auto ppos = rest.find("p=");
        if (ppos != std::string::npos && ppos < seqtok) {
            std::size_t ps = ppos + 2;
            std::size_t pe = rest.find(' ', ps);
            c.p = std::stol(rest.substr(ps, pe - ps), nullptr, 16);
        }
        const auto spos = rest.find("seg=");
        if (spos != std::string::npos && spos < seqtok) {
            std::size_t ss = spos + 4;
            std::size_t se = rest.find(' ', ss);
            c.seg = std::stol(rest.substr(ss, se - ss), nullptr, 16);
        }
        std::string seq = rest.substr(seqtok + 4);
        std::istringstream ss(seq);
        std::string item;
        while (std::getline(ss, item, ';')) {
            if (item.empty()) continue;
            std::vector<std::string> f;
            std::size_t start = 0;
            for (;;) {
                std::size_t comma = item.find(',', start);
                if (comma == std::string::npos) {
                    f.push_back(item.substr(start));
                    break;
                }
                f.push_back(item.substr(start, comma - start));
                start = comma + 1;
            }
            WorldCall wc;
            wc.tag = f[0];
            wc.a = f.size() > 1 ? std::stol(f[1]) : 0;
            wc.b = f.size() > 2 ? std::stol(f[2]) : 0;
            wc.c = f.size() > 3 ? std::stol(f[3]) : 0;
            c.seq.push_back(wc);
        }
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtFreeTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7c93e8_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }
};

// 逐用例：外部世界调用序列（标签+参数+顺序）与 golden 逐位一致
TEST_F(CrtFreeTest, GoldenWorldCallSequenceMatches) {
    for (const GCase& c : cases_) {
        std::vector<WorldCall> calls;
        g_calls = &calls;
        bool find_hit = (c.tag != "MISS");
        g_find_hit = &find_hit;
        g_find_seg = (void*)(intptr_t)c.seg;
        ra2::runtime::CrtFreeWorld w = MakeWorld(&find_hit);
        ra2::runtime::free_legacy(&w, (void*)(intptr_t)c.p);
        g_calls = nullptr;

        ASSERT_EQ(calls.size(), c.seq.size()) << "case " << c.tag;
        for (std::size_t i = 0; i < calls.size(); ++i) {
            EXPECT_TRUE(calls[i] == c.seq[i])
                << "case " << c.tag << " call " << i << ": got "
                << calls[i].tag << "," << calls[i].a << "," << calls[i].b
                << "," << calls[i].c;
        }
    }
}

// NULL → 零调用（不碰锁/堆）
TEST_F(CrtFreeTest, NullNoCalls) {
    std::vector<WorldCall> calls;
    g_calls = &calls;
    bool hit = true;
    g_find_hit = &hit;
    ra2::runtime::CrtFreeWorld w = MakeWorld(&hit);
    ra2::runtime::free_legacy(&w, nullptr);
    g_calls = nullptr;
    EXPECT_TRUE(calls.empty());
}

// 命中断言：顺序 = ENTER(9) → FIND(p) → FREESEG(seg,p) → LEAVE(9)
TEST_F(CrtFreeTest, HitOrder) {
    std::vector<WorldCall> calls;
    g_calls = &calls;
    bool hit = true;
    g_find_hit = &hit;
    g_find_seg = (void*)0x5678;  // 命中段注入（golden HIT 用例同值）
    ra2::runtime::CrtFreeWorld w = MakeWorld(&hit);
    ra2::runtime::free_legacy(&w, (void*)0x1000);
    g_calls = nullptr;
    ASSERT_EQ(calls.size(), 4u);
    EXPECT_EQ(calls[0].tag, "ENTER");  EXPECT_EQ(calls[0].a, 9);
    EXPECT_EQ(calls[1].tag, "FIND");   EXPECT_EQ(calls[1].a, 0x1000);
    EXPECT_EQ(calls[2].tag, "FREESEG"); EXPECT_EQ(calls[2].a, 0x5678);
    EXPECT_EQ(calls[2].b, 0x1000);
    EXPECT_EQ(calls[3].tag, "LEAVE");  EXPECT_EQ(calls[3].a, 9);
}

// 未命中断言：顺序 = ENTER(9) → FIND(p) → LEAVE(9) → HEAPFREE(heap,0,p)
TEST_F(CrtFreeTest, MissOrder) {
    std::vector<WorldCall> calls;
    g_calls = &calls;
    bool hit = false;
    g_find_hit = &hit;
    ra2::runtime::CrtFreeWorld w = MakeWorld(&hit);
    ra2::runtime::free_legacy(&w, (void*)0x2000);
    g_calls = nullptr;
    ASSERT_EQ(calls.size(), 4u);
    EXPECT_EQ(calls[0].tag, "ENTER");  EXPECT_EQ(calls[0].a, 9);
    EXPECT_EQ(calls[1].tag, "FIND");   EXPECT_EQ(calls[1].a, 0x2000);
    EXPECT_EQ(calls[2].tag, "LEAVE");  EXPECT_EQ(calls[2].a, 9);
    EXPECT_EQ(calls[3].tag, "HEAPFREE");
    EXPECT_EQ(calls[3].a, 0x12345678);  // heap handle
    EXPECT_EQ(calls[3].b, 0);           // flags
    EXPECT_EQ(calls[3].c, 0x2000);      // p
}