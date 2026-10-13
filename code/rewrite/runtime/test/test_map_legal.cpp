#include "../map_legal.h"

#include <gtest/gtest.h>

namespace {

using ra2::runtime::map_coordinates_legal;

}  // namespace

// 0x568316: x+y <= W → 非法。W=8 时 (1,1) 的 x+y=2。
TEST(MapLegalTest, SumBelowOrEqualWIsIllegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 1, 1));
    EXPECT_FALSE(map_coordinates_legal(8, 8, 4, 4));
}

// 0x56831F: x-y >= W → 非法。(10,0) 差 10 ≥ 8。
TEST(MapLegalTest, XMinusYBeyondWIsIllegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 10, 0));
}

// 0x568326: y-x >= W → 非法。(0,10) 差 10 ≥ 8。
TEST(MapLegalTest, YMinusXBeyondWIsIllegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 0, 10));
}

// 0x568333: x+y > W+2H → 非法。W=8,H=8 → W+2H=24；(13,12) 和 25。
TEST(MapLegalTest, SumBeyondWPlus2HIsIllegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 13, 12));
}

// 菱形内：x+y 在 (W, W+2H]、|x-y| < W。(5,5) 差 0、和 10 合法；
// (11,4) 差 7（<8）、和 15 合法；(8,0) 和 8 正好触下界（jle 判非法）。
TEST(MapLegalTest, InsideDiamondIsLegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 8, 0));
    EXPECT_TRUE(map_coordinates_legal(8, 8, 11, 4));
    EXPECT_TRUE(map_coordinates_legal(8, 8, 5, 5));
}

// 上边界等号：x+y == W+2H 合法（jle/jg 判定：等于不越界）。
TEST(MapLegalTest, SumAtUpperBoundaryIsLegal) {
    EXPECT_TRUE(map_coordinates_legal(8, 8, 13, 11));
    EXPECT_FALSE(map_coordinates_legal(8, 8, 13, 12));
}

// x-y 等号边界：x-y == W 非法（jge）。W=8 时 (9,1) 差 8。
TEST(MapLegalTest, DiffAtBoundaryIsIllegal) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 9, 1));
}

// 负坐标：movsx 符号扩展后仍按同一判据（原版行为照抄）。W=8 时
// x+y 必须 > 8，故 (5,-2)=3、(6,-1)=5 均落在下界外 → 非法；
// (7,2)=9 且 y-x=-5<8、x-y=5<8 → 合法（合法区可含负 x）。
TEST(MapLegalTest, NegativeCoordsFollowSamePredicate) {
    EXPECT_FALSE(map_coordinates_legal(8, 8, 5, -2));
    EXPECT_FALSE(map_coordinates_legal(8, 8, 6, -1));
    EXPECT_TRUE(map_coordinates_legal(8, 8, 7, 2));
}