// _itoa 家族 golden 对拍 —— batch 1043
//
// 0x7D46B9(92B) itoa 核心（_ltoa/_ultoa 共享格式化器）+ 0x7D473F
// `_ultoa`（27B 薄包装）+ 0x7D4715 `_ltoa`（42B 薄包装）。
// golden 由 Unicorn 指令级模拟原版机器码取得：
//   deadcode_7d46b9_golden.txt（12 用例）——CASE value=.. radix=..
//     sign=.. -> eax=0x%08x BUF=<hex 64B>
//   deadcode_7d473f_golden.txt（10 用例）——CASE value=.. radix=..
//     -> eax=0x%08x BUF=<hex 64B>
//   deadcode_7d4715_golden.txt（10 用例）——同 _ultoa 格式。
// 对拍点：写出的 buffer 64 字节 + 核心 eax 残留（反转循环最后 al）。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "crt_itoa.h"

namespace {

struct Case {
    int value;
    int radix;
    int sign;      // 核心才有；包装 golden 行无 sign 字段
    unsigned eax;  // 核心 = 残留；包装 = buffer 地址（0x20006000，忽略）
    std::string buf;  // 64 字节 hex 串
};

std::string TrimCr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

std::vector<std::string> SplitWs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<Case> LoadCoreGolden() {
    std::vector<Case> out;
    const std::string path = std::string(RA2_GOLDEN_DIR) +
                              "/deadcode_7d46b9_golden.txt";
    std::FILE* f = std::fopen(path.c_str(), "r");
    EXPECT_TRUE(f != nullptr);
    if (!f) return out;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        std::string s = TrimCr(line);
        if (s.empty() || s[0] == '#') continue;
        std::vector<std::string> p = SplitWs(s);
        if (p.size() < 7) continue;
        // p[0]=CASE p[1]=value=N p[2]=radix=N p[3]=sign=N p[4]=->
        // p[5]=eax=0x.. p[6]=BUF=<hex>
        Case c;
        c.value = std::atoi(p[1].c_str() + 6);
        c.radix = std::atoi(p[2].c_str() + 6);
        c.sign = std::atoi(p[3].c_str() + 5);
        c.eax = static_cast<unsigned>(
            std::strtoul(p[5].c_str() + 4, nullptr, 16));
        c.buf = p[6].substr(4);
        out.push_back(c);
    }
    std::fclose(f);
    return out;
}

std::vector<Case> LoadWrapperGolden(const char* name) {
    std::vector<Case> out;
    std::FILE* f = std::fopen((std::string(RA2_GOLDEN_DIR) + "/" + name).c_str(),
                              "r");
    EXPECT_TRUE(f != nullptr);
    if (!f) return out;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        std::string s = TrimCr(line);
        if (s.empty() || s[0] == '#') continue;
        std::vector<std::string> p = SplitWs(s);
        if (p.size() < 6) continue;
        // p[0]=CASE p[1]=value=N p[2]=radix=N p[3]=-> p[4]=eax=0x..
        // p[5]=BUF=<hex>
        Case c;
        c.value = std::atoi(p[1].c_str() + 6);
        c.radix = std::atoi(p[2].c_str() + 6);
        c.sign = -1;  // 无 sign 字段
        c.eax = static_cast<unsigned>(
            std::strtoul(p[4].c_str() + 4, nullptr, 16));
        c.buf = p[5].substr(4);
        out.push_back(c);
    }
    std::fclose(f);
    return out;
}

std::vector<char> HexToBytes(const std::string& hex) {
    std::vector<char> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back(static_cast<char>((nib(hex[i]) << 4) | nib(hex[i + 1])));
    }
    return out;
}

}  // namespace

// ---- 核心 0x7D46B9：12 用例 buffer + eax 残留逐位对拍 ----

TEST(ItoaCore, GoldenMatches) {
    std::vector<Case> cases = LoadCoreGolden();
    ASSERT_GE(cases.size(), 12u);
    for (const Case& c : cases) {
        char buf[64];
        std::memset(buf, 0, sizeof buf);
        unsigned eax = ra2::crt::itoa_core_legacy(c.value, buf, c.radix,
                                                       c.sign);
        std::vector<char> want = HexToBytes(c.buf);
        ASSERT_EQ(want.size(), sizeof buf);
        EXPECT_EQ(std::memcmp(buf, want.data(), sizeof buf), 0)
            << "core value=" << c.value << " radix=" << c.radix
            << " sign=" << c.sign;
        EXPECT_EQ(eax, c.eax)
            << "core eax value=" << c.value << " radix=" << c.radix
            << " sign=" << c.sign;
    }
}

// ---- _ultoa 0x7D473F：10 用例 buffer 对拍 + 返回 buffer ----

TEST(UltOa, GoldenMatches) {
    std::vector<Case> cases = LoadWrapperGolden("deadcode_7d473f_golden.txt");
    ASSERT_GE(cases.size(), 10u);
    for (const Case& c : cases) {
        char buf[64];
        std::memset(buf, 0, sizeof buf);
        char* ret = ra2::crt::ultoa_legacy(
            static_cast<unsigned>(c.value), buf, c.radix);
        std::vector<char> want = HexToBytes(c.buf);
        EXPECT_EQ(std::memcmp(buf, want.data(), sizeof buf), 0)
            << "ultoa value=" << c.value << " radix=" << c.radix;
        EXPECT_EQ(ret, buf);
    }
}

// ---- _ltoa 0x7D4715：10 用例 buffer 对拍 + 返回 buffer ----

TEST(Ltoa, GoldenMatches) {
    std::vector<Case> cases = LoadWrapperGolden("deadcode_7d4715_golden.txt");
    ASSERT_GE(cases.size(), 10u);
    for (const Case& c : cases) {
        char buf[64];
        std::memset(buf, 0, sizeof buf);
        char* ret = ra2::crt::ltoa_legacy(c.value, buf, c.radix);
        std::vector<char> want = HexToBytes(c.buf);
        EXPECT_EQ(std::memcmp(buf, want.data(), sizeof buf), 0)
            << "ltoa value=" << c.value << " radix=" << c.radix;
        EXPECT_EQ(ret, buf);
    }
}

// ---- 边界与怪癖显式断言 ----

TEST(ItoaCore, Quirks) {
    char buf[64];
    // ① sign=1 无条件取负——正数也会翻转（golden 实证 2147483647 →
    //    "-2147483649"）
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(2147483647, buf, 10, 1);
    EXPECT_STREQ(buf, "-2147483649");
    // ② sign=0 时负值按无符号位型除（_ultoa 语义）
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(-12345, buf, 10, 0);
    EXPECT_STREQ(buf, "4294954951");
    // ③ 余数 >9 小写 hex
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(0xABCDEF, buf, 16, 0);
    EXPECT_STREQ(buf, "abcdef");
    // ④ INT_MIN 取负自反 → 2147483648
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(-2147483648, buf, 10, 1);
    EXPECT_STREQ(buf, "-2147483648");
    // ⑤ 零 → 单字符 "0"
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(0, buf, 10, 0);
    EXPECT_STREQ(buf, "0");
    // ⑥ 二进制/八进制
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(8, buf, 2, 0);
    EXPECT_STREQ(buf, "1000");
    std::memset(buf, 0, sizeof buf);
    ra2::crt::itoa_core_legacy(777, buf, 8, 0);
    EXPECT_STREQ(buf, "1411");
}

TEST(UltOa, Quirks) {
    char buf[64];
    // _ultoa 恒无符号（sign=0）
    std::memset(buf, 0, sizeof buf);
    ra2::crt::ultoa_legacy(0xFFFFFF00u, buf, 16);
    EXPECT_STREQ(buf, "ffffff00");
    std::memset(buf, 0, sizeof buf);
    ra2::crt::ultoa_legacy(4294954951u, buf, 10);
    EXPECT_STREQ(buf, "4294954951");
}

TEST(Ltoa, Quirks) {
    char buf[64];
    // 十进制负号判定
    std::memset(buf, 0, sizeof buf);
    ra2::crt::ltoa_legacy(-12345, buf, 10);
    EXPECT_STREQ(buf, "-12345");
    // radix != 10 → 无负号无符号 hex
    std::memset(buf, 0, sizeof buf);
    ra2::crt::ltoa_legacy(-12345, buf, 16);
    EXPECT_STREQ(buf, "ffffcfc7");
}

// ---- 与 snprintf 交叉参考（十进制/十六进制/八进制）----

TEST(ItoaCore, ReferenceCrossCheck) {
    char buf[64];
    const int dec_vals[] = {0, 1, -1, 12345, -12345, 2147483647,
                            -2147483647 - 1};  // INT_MIN 字面量防溢出
    for (int v : dec_vals) {
        std::memset(buf, 0, sizeof buf);
        ra2::crt::ltoa_legacy(v, buf, 10);
        char want[64];
        std::snprintf(want, sizeof want, "%d", v);
        EXPECT_STREQ(buf, want) << "decimal v=" << v;
    }
    const unsigned hex_vals[] = {0u, 1u, 255u, 0xABCDEFu, 0xFFFFFFFFu,
                                  12345u};
    for (unsigned v : hex_vals) {
        std::memset(buf, 0, sizeof buf);
        ra2::crt::ultoa_legacy(v, buf, 16);
        char want[64];
        std::snprintf(want, sizeof want, "%x", v);
        EXPECT_STREQ(buf, want) << "hex v=" << v;
        std::memset(buf, 0, sizeof buf);
        ra2::crt::ultoa_legacy(v, buf, 8);
        std::snprintf(want, sizeof want, "%o", v);
        EXPECT_STREQ(buf, want) << "oct v=" << v;
    }
}