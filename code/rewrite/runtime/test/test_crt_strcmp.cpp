#include "../crt_strcmp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> parse_hex(std::string tok) {
    const auto ns = tok.find_first_not_of(' ');
    if (ns != std::string::npos) tok.erase(0, ns);
    std::vector<std::uint8_t> out;
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

// golden 行：CASE s1=<hex> s2=<hex> a1=<align0-3> -> ret=<tri>
struct GCase {
    std::vector<std::uint8_t> s1;
    std::vector<std::uint8_t> s2;
    int a1 = 0;
    int ret = 0;
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
        c.s1 = parse_hex(toks[1].substr(3));
        c.s2 = parse_hex(toks[2].substr(3));
        c.a1 = std::stoi(toks[3].substr(3));
        c.ret = std::stoi(toks[5].substr(4));
        out->push_back(c);
    }
    return true;
}

// 独立参照实现（标准 strcmp C locale 三态语义：逐字节无符号比较，
// NUL 处理；返回三态 {-1,0,+1} 与原版 sbb/shl/inc 一致）
int ref_strcmp(const char* a, const char* b) {
    const auto* x = reinterpret_cast<const std::uint8_t*>(a);
    const auto* y = reinterpret_cast<const std::uint8_t*>(b);
    while (*x && *x == *y) {
        ++x;
        ++y;
    }
    if (*x == *y) return 0;
    return *x < *y ? -1 : 1;
}

}  // namespace

class CrtStrcmpTest : public ::testing::Test {
 protected:
    std::vector<GCase> cases_;

    void SetUp() override {
        const std::string path =
            std::string(RA2_GOLDEN_DIR) + "/deadcode_7cda90_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }

    // 将字节串（含尾 NUL）拷入对齐缓冲，返回对齐后的指针
    static const char* aligned_buf(const std::vector<std::uint8_t>& bytes,
                                   int a1, char* store, std::size_t cap) {
        if (cap < 64u) return nullptr;
        const std::uintptr_t base =
            reinterpret_cast<std::uintptr_t>(store);
        // 找偏移 k ∈ [0,3) 使 (base+k+1)%4 == a1（预留 1 字节避免 store
        // 本身被占用）——简化：直接挑 k 使 (base+k)%4==a1，k 可能为 0
        // 但 store 由调用者保证 ≥64B，安全。
        const std::uintptr_t cur = base % 4;
        const std::size_t k = static_cast<std::size_t>((a1 - cur + 4) % 4);
        char* p = store + k;
        std::memset(p, 0xCC, 48);  // 哨兵：窗口越界读 1-3 字节不参与比较
        for (std::size_t i = 0; i < bytes.size(); ++i) p[i] =
            static_cast<char>(bytes[i]);
        return p;
    }
};

// 逐用例：三态与 golden 一致（s1/s2 含尾 NUL；a1 控制 s1 对齐覆盖慢路径）
TEST_F(CrtStrcmpTest, GoldenMatches) {
    for (const GCase& c : cases_) {
        char store1[64];
        char store2[64];
        const char* s1 = aligned_buf(c.s1, c.a1, store1, sizeof(store1));
        std::memset(store2, 0x22, 16);  // s2 任意对齐
        for (std::size_t i = 0; i < c.s2.size(); ++i)
            store2[i] = static_cast<char>(c.s2[i]);
        const int got = ra2::runtime::strcmp_legacy(s1, store2);
        EXPECT_EQ(got, c.ret) << "a1=" << c.a1
                              << " s1.size=" << c.s1.size()
                              << " s2.size=" << c.s2.size();
    }
}

// 边界：双空/单空两向/大小写敏感/高字节无符号/NUL 在窗口不同位置
TEST_F(CrtStrcmpTest, BoundaryCases) {
    EXPECT_EQ(ra2::runtime::strcmp_legacy("", ""), 0);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("", "a"), -1);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("a", ""), 1);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("abc", "abd"), -1);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("abc", "abc"), 0);
    // 大小写敏感（对照 strnicmp 的 0）
    EXPECT_EQ(ra2::runtime::strcmp_legacy("AbC", "aBc"), -1);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("aBc", "AbC"), 1);
    // 前置相等、尾部 s1 到 NUL vs s2 更长
    EXPECT_EQ(ra2::runtime::strcmp_legacy("ab", "abc"), -1);
    EXPECT_EQ(ra2::runtime::strcmp_legacy("abc", "ab"), 1);
    // 高字节无符号：0xFF > 0xFE
    const char hi1[] = {(char)0xFF, (char)0x01, 0};
    const char hi2[] = {(char)0xFE, (char)0x01, 0};
    EXPECT_EQ(ra2::runtime::strcmp_legacy(hi1, hi2), 1);
    const char hi3[] = {(char)0x01, (char)0xFF, 0};
    const char hi4[] = {(char)0x01, (char)0xFE, 0};
    EXPECT_EQ(ra2::runtime::strcmp_legacy(hi3, hi4), 1);
    // 16 字符长相等（跨 4 窗口）
    EXPECT_EQ(ra2::runtime::strcmp_legacy("0123456789abcdef",
                                          "0123456789abcdef"),
              0);
    // NUL 在窗口 b3：abcd
    EXPECT_EQ(ra2::runtime::strcmp_legacy("abcd", "abcd"), 0);
}

// 参照抽查：随机字节串（0-127+少量高位）× 随机位移 vs 独立参照实现
TEST_F(CrtStrcmpTest, ReferenceCrossCheck) {
    std::uint32_t seed = 0x7CDA90u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int t = 0; t < 96; ++t) {
        const int l1 = static_cast<int>(rnd() % 14);
        const int l2 = static_cast<int>(rnd() % 14);
        char a[64];
        char b[64];
        std::memset(a, 0xA5, sizeof(a));
        std::memset(b, 0x5A, sizeof(b));
        for (int i = 0; i < l1; ++i) {
            char c = static_cast<char>(rnd() % 200);
            if (c == 0) c = 'x';
            a[i] = c;
        }
        for (int i = 0; i < l2; ++i) {
            char c = static_cast<char>(rnd() % 200);
            if (c == 0) c = 'y';
            b[i] = c;
        }
        a[l1] = 0;
        b[l2] = 0;
        // 随机 s1 偏移覆盖慢路径对齐（0-3）
        const std::size_t shift = static_cast<std::size_t>(rnd() % 4);
        char as[72];
        std::memset(as, 0xCC, sizeof(as));
        std::memcpy(as + shift, a, 16);
        const int got =
            ra2::runtime::strcmp_legacy(as + shift, b);
        const int ref = ref_strcmp(as + shift, b);
        EXPECT_EQ(got, ref) << "pair " << t << " l1=" << l1
                            << " l2=" << l2 << " shift=" << shift;
    }
}