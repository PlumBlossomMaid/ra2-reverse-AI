#include "../ra2_string.h"

#include <gtest/gtest.h>

namespace {

const char kHello[] = "hello";

}  // namespace

// 0x7B5400 / 0x7B5440 c_str()：非 NULL 时返回内部指针原样。
TEST(Ra2StringTest, CStrReturnsInternalPointer) {
    const char* buf = kHello;
    const void* self = &buf;
    EXPECT_EQ(ra2::runtime::narrow_cstr_legacy(self), kHello);
}

// 0x7B5400 / 0x7B5440 c_str()：NULL 时回退共享空串常量（不回 nullptr）。
TEST(Ra2StringTest, CStrFallsBackToSharedEmptyOnNull) {
    const char* buf = nullptr;
    const void* self = &buf;
    EXPECT_NE(ra2::runtime::narrow_cstr_legacy(self), nullptr);
    EXPECT_EQ(ra2::runtime::narrow_cstr_legacy(self)[0], '\0');
}

// 0x7B5410 / 0x7B5450 / 0x7B5480 operator[]：正常下标读字符。
TEST(Ra2StringTest, CharAtReadsWithinBounds) {
    const char* buf = kHello;
    const void* self = &buf;
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 0), 'h');
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 4), 'o');
}

// ★ 越界检查：index >= strlen 返回 0（原版 jae 分支），不是未定义行为。
TEST(Ra2StringTest, CharAtClampsOutOfBoundsToZero) {
    const char* buf = kHello;  // strlen = 5
    const void* self = &buf;
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 5), '\0');
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 100), '\0');
}

// 空串（buf = ""）：len = 0，任何下标都越界返 0；index==0 也返 0。
TEST(Ra2StringTest, CharAtOnEmptyStringIsAlwaysZero) {
    const char* buf = "";
    const void* self = &buf;
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 0), '\0');
    EXPECT_EQ(ra2::runtime::narrow_char_at_legacy(self, 1), '\0');
}

// 0x7B4780 operator==：双方皆 NULL 判等（0x7B4790 mov al,1）。
TEST(Ra2StringTest, EqualsTreatsTwoNullsAsEqual) {
    const char* a = nullptr;
    const char* b = nullptr;
    EXPECT_TRUE(ra2::runtime::narrow_equals_legacy(&a, &b));
}

// 一方 NULL 一方非 NULL 判不等（0x7B47E0 xor al,al）。
TEST(Ra2StringTest, EqualsRejectsNullAgainstNonNull) {
    const char* a = nullptr;
    const char* b = kHello;
    EXPECT_FALSE(ra2::runtime::narrow_equals_legacy(&a, &b));
    EXPECT_FALSE(ra2::runtime::narrow_equals_legacy(&b, &a));
}

// 内容相同判等（走 2 字节展开循环）。
TEST(Ra2StringTest, EqualsAcceptsIdenticalStrings) {
    const char* a = kHello;
    const char* b = "hello";
    EXPECT_TRUE(ra2::runtime::narrow_equals_legacy(&a, &b));
}

// 首字节不同即不等，不继续比较（0x7B47A9 jne → 0x7B47D1 sbb 归一化）。
TEST(Ra2StringTest, EqualsRejectsOnFirstDifferingByte) {
    const char* a = kHello;
    const char* b = "hellp";
    EXPECT_FALSE(ra2::runtime::narrow_equals_legacy(&a, &b));
    // 前缀相同、长度不同也算不等。
    const char* c = "hell";
    EXPECT_FALSE(ra2::runtime::narrow_equals_legacy(&a, &c));
}

// 两个空串（非 NULL 指针）判等——循环首个字节即 '\0'。
TEST(Ra2StringTest, EqualsAcceptsTwoEmptyStrings) {
    const char* a = "";
    const char* b = "";
    EXPECT_TRUE(ra2::runtime::narrow_equals_legacy(&a, &b));
}

// 奇数长度串走展开循环的第二个字节分支（"abc" 长度 3，2 字节展开需读 a[3]）。
TEST(Ra2StringTest, EqualsHandlesOddLengthStrings) {
    const char* a = "abc";
    const char* b = "abc";
    EXPECT_TRUE(ra2::runtime::narrow_equals_legacy(&a, &b));
    const char* c = "abd";
    EXPECT_FALSE(ra2::runtime::narrow_equals_legacy(&a, &c));
}