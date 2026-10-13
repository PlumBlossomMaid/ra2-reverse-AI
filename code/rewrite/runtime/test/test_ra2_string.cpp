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