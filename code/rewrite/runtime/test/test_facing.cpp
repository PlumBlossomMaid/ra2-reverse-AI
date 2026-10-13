#include "../facing.h"

#include <gtest/gtest.h>

namespace {

using ra2::runtime::FacingStruct;

// ROT 是 INI 值 ×256，ctor 里 clamp 到 127 再左移 8 位。
constexpr std::uint16_t Rot(std::uint32_t ini) {
    return static_cast<std::uint16_t>((ini > 0x7Fu ? 0x7Fu : ini) << 8);
}

FacingStruct Make(std::uint16_t value, std::uint16_t initial, std::uint32_t rot,
                  std::uint32_t start_time, std::uint32_t time_left) {
    FacingStruct f{};
    f.Value = value;
    f.Initial = initial;
    f.ROT = Rot(rot);
    f.StartTime = start_time;
    f.TimeLeft = time_left;
    return f;
}

constexpr std::uint32_t kNotStarted = 0xFFFFFFFFu;

}  // namespace

// ---------------------------------------------------------------- ctor 0x4C91E0

TEST(FacingCtorTest, RotIsIniValueTimes256) {
    ra2::runtime::FacingStruct f{};
    ra2::runtime::facing_construct(&f, 2, 1000);
    EXPECT_EQ(f.ROT, 0x0200);
    EXPECT_EQ(f.Value, 0);
    EXPECT_EQ(f.Initial, 0);
    EXPECT_EQ(f.TimeLeft, 0);
    EXPECT_EQ(f.StartTime, 1000u);  // StartTime = 构造时的 CurrentFrame
}

// 0x4C91FF: cmp ecx,0x7f / jl skip / mov ecx,0x7f —— 超过 127 被夹到 127。
TEST(FacingCtorTest, RotClampsTo127BeforeShift) {
    ra2::runtime::FacingStruct f{};
    ra2::runtime::facing_construct(&f, 200, 0);
    EXPECT_EQ(f.ROT, 0x7F00);
}

// -------------------------------------------------- timer 0x426630 (TimerStruct)

TEST(TimerGetTimeLeftTest, StoppedTimerReportsStoredTimeLeft) {
    EXPECT_EQ(ra2::runtime::timer_get_time_left(kNotStarted, 42, 100), 42u);
}

TEST(TimerGetTimeLeftTest, TickingSubtractsElapsedFrames) {
    // StartTime=1000, TimeLeft=50, frame=1010 → 剩 40
    EXPECT_EQ(ra2::runtime::timer_get_time_left(1000, 50, 1010), 40u);
}

// 0x426644: jge 0x426649 —— elapsed >= TimeLeft 时钳到 0（不是负数）。
TEST(TimerGetTimeLeftTest, ExpiredTimerClampsToZero) {
    EXPECT_EQ(ra2::runtime::timer_get_time_left(1000, 50, 1050), 0u);
    EXPECT_EQ(ra2::runtime::timer_get_time_left(1000, 50, 2000), 0u);
}

// ------------------------------------------------- GetFacing 0x4C93D0 (current)

TEST(FacingGetTest, NoRateReturnsValueImmediately) {
    // ROT=0：0x4C93DB jle → 直接回读 Value
    FacingStruct f = Make(0x1000, 0x0800, 0, 1000, 30);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1000), 0x1000u);
}

// 0x4C93F2: elapsed >= TimeLeft → 原样返回 Value（旋转已结束）。
TEST(FacingGetTest, ExpiredTimerReturnsValue) {
    FacingStruct f = Make(0x1000, 0x0800, 2, 1000, 30);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1030), 0x1000u);
}

// remaining==0（0x4C93FA）走原样返回；此例 ROT=0 也覆盖不了，用 TimeLeft=0。
TEST(FacingGetTest, ZeroTimeLeftReturnsValue) {
    FacingStruct f = Make(0x1000, 0x0800, 2, kNotStarted, 0);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1000), 0x1000u);
}

// steps = |diff|/ROT 为 0（|diff| < ROT）时 0x4C941C jle → 不插值。
TEST(FacingGetTest, StepsBelowOneSkipsInterpolation) {
    // diff = 0x1000 - 0x0F00 = 0x100，ROT=2（=0x200）→ |diff|/ROT = 0
    FacingStruct f = Make(0x1000, 0x0F00, 2, 1000, 30);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1000), 0x1000u);
}

// 正常插值（自洽参数：SetDesired 会给 TimeLeft = |diff|/ROT = 16）。
// diff = 0x1000，ROT=1（=0x100）→ steps = 0x1000/0x100 = 0x10。
// per_step = diff/steps = 0x1000/0x10 = 0x100；remaining = TimeLeft = 16。
// out = Value - 0x100*16 = 0x1000 - 0x1000 = 0（旋转起点=Initial）。
TEST(FacingGetTest, InterpolatesBetweenInitialAndValue) {
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 0x10);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1000), 0x0000u);
}

// 时间推进 → remaining 递减 → 当前朝向向 Value 逼近。
// 帧 1008：elapsed=8，remaining=8 → out = 0x1000 - 0x100*8 = 0x800。
TEST(FacingGetTest, RemainingFramesDriveInterpolation) {
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 0x10);
    EXPECT_EQ(ra2::runtime::facing_get_facing(f, 1008), 0x0800u);
}

// ★ 关键：机器码是「先除后乘」idiv 再 imul（0x4C943B/0x4C943D）。
// YRpp 头文件写的是「先乘后除」steps_left*diff/num_steps（截断点不同）。
// 此例让两种顺序给出不同结果，把差异钉死在单测里。
//   diff = 0x0007 = 7，ROT=1（=0x100）→ steps = 7/0x100 = 0 → 不插值。
// 换 ROT=0x40（ini 0.25 不行，取 ini 值 1 → ROT=0x100）：
//   steps = 7 / 0x100 = 0，跳过。取 diff 更大：
//   diff = 0x7FF = 2047，ROT=0x100=256 → steps = 7。
//   per_step = 2047/7 = 292（先除后乘）；若先乘后除则 30*2047/7 = 8772。
//   remaining = 10 → 机器码 out = 2047 - 292*10 = 2047-2920 = -873 → 16 位回绕。
//   目标 Value = 2047 + Initial，取 Initial=0。
TEST(FacingGetTest, DivideBeforeMultiplyMatchesMachineCode) {
    FacingStruct f = Make(0x07FF, 0x0000, 1, 1000, 30);
    // 帧 1020 → remaining = 10
    const std::uint32_t got = ra2::runtime::facing_get_facing(f, 1020);
    // 先除后乘：per_step = 2047/7 = 292；out = 2047 - 292*10 = -873
    //   16 位截断：-873 = 0xFC97 → 0x07FF - 0xFC97 = 0x0B68（回绕）
    const std::int32_t per_step = 2047 / 7;
    const std::int32_t drop = static_cast<std::int16_t>(per_step * 10);
    const std::uint32_t expect =
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(0x07FF - drop));
    EXPECT_EQ(got, expect);
    EXPECT_NE(got, static_cast<std::uint32_t>(0x07FF - 8772));  // 先乘后除会给别的值
}

// ------------------------------------------- IsRotating 0x4C9480 / CW / CCW

TEST(FacingIsRotatingTest, NoRateIsNeverRotating) {
    FacingStruct f = Make(0x1000, 0x0800, 0, 1000, 30);
    EXPECT_FALSE(ra2::runtime::facing_is_rotating(f, 1000));
}

TEST(FacingIsRotatingTest, RunningTimerCountsAsRotating) {
    FacingStruct f = Make(0x1000, 0x0800, 2, 1000, 30);
    EXPECT_TRUE(ra2::runtime::facing_is_rotating(f, 1000));
    EXPECT_TRUE(ra2::runtime::facing_is_rotating(f, 1020));
    // 帧 1030：elapsed=30 >= TimeLeft → 结束
    EXPECT_FALSE(ra2::runtime::facing_is_rotating(f, 1030));
}

// 0x4C94B0 setg：diff > 0 判顺时针。
TEST(FacingDirectionTest, PositiveDiffIsClockwise) {
    FacingStruct f = Make(0x2000, 0x1000, 2, 1000, 30);
    EXPECT_TRUE(ra2::runtime::facing_is_rotating_cw(f, 1000));
    EXPECT_FALSE(ra2::runtime::facing_is_rotating_ccw(f, 1000));
}

// 0x4C9522 sets：diff < 0 判逆时针。
TEST(FacingDirectionTest, NegativeDiffIsCounterClockwise) {
    FacingStruct f = Make(0x1000, 0x2000, 2, 1000, 30);
    EXPECT_FALSE(ra2::runtime::facing_is_rotating_cw(f, 1000));
    EXPECT_TRUE(ra2::runtime::facing_is_rotating_ccw(f, 1000));
}

// 不在旋转时 CW/CCW 一律 false（0x4C94B0/0x4C94F0 都先过 IsRotating 门）。
TEST(FacingDirectionTest, NotRotatingGivesNoDirection) {
    FacingStruct f = Make(0x2000, 0x1000, 0, 1000, 30);
    EXPECT_FALSE(ra2::runtime::facing_is_rotating_cw(f, 1000));
    EXPECT_FALSE(ra2::runtime::facing_is_rotating_ccw(f, 1000));
}

// ------------------------------------------------- SetDesired 0x4C9220

// 0x4C922D: Value == target → 直接 false，字段不动。
TEST(FacingSetDesiredTest, SameTargetIsNoOp) {
    FacingStruct f = Make(0x1000, 0x1000, 2, 1000, 0);
    EXPECT_FALSE(ra2::runtime::facing_set_desired(f, 0x1000, 1000));
    EXPECT_EQ(f.Value, 0x1000);
    EXPECT_EQ(f.Initial, 0x1000);
}

// 0x4C92B1: Initial 记的是「转向此刻的实际朝向」。
TEST(FacingSetDesiredTest, InitialRecordsCurrentFacingNotOldValue) {
    // 先让它转到一半，再改目标：Initial 应为中途朝向。
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 30);
    const std::uint16_t midway = static_cast<std::uint16_t>(
        ra2::runtime::facing_get_facing(f, 1015));
    ASSERT_EQ(ra2::runtime::facing_set_desired(f, 0x2000, 1015), true);
    EXPECT_EQ(f.Initial, midway);
    EXPECT_EQ(f.Value, 0x2000);
}

// 0x4C92DA: TimeLeft = |Value-Initial|/ROT，StartTime = CurrentFrame。
TEST(FacingSetDesiredTest, ArmsTimerWithStepCountAsDuration) {
    // ROT ini=1 → 0x100；diff = 0x2000 → TimeLeft = 0x2000/0x100 = 0x20
    FacingStruct f = Make(0x1000, 0x1000, 1, 1000, 0);
    ASSERT_TRUE(ra2::runtime::facing_set_desired(f, 0x3000, 5000));
    EXPECT_EQ(f.StartTime, 5000u);
    EXPECT_EQ(f.TimeLeft, 0x20u);
}

// 0x4C92BF: ROT<=0 → 立即到位，不起步定时器（StartTime 不动）。
TEST(FacingSetDesiredTest, NoRateSnapsWithoutTimer) {
    FacingStruct f = Make(0x1000, 0x1000, 0, 1000, 7);
    ASSERT_TRUE(ra2::runtime::facing_set_desired(f, 0x3000, 5000));
    EXPECT_EQ(f.Value, 0x3000);
    EXPECT_EQ(f.Initial, 0x1000);
    EXPECT_EQ(f.StartTime, 1000u);
    EXPECT_EQ(f.TimeLeft, 7u);
}

// -------------------------------------------- Difference 0x4C9530 / 0x4C95D0

TEST(FacingDifferenceTest, DifferenceIsValueMinusCurrent) {
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 30);
    const std::uint16_t now =
        static_cast<std::uint16_t>(ra2::runtime::facing_get_facing(f, 1015));
    const std::int16_t diff = ra2::runtime::facing_difference(f, 1015);
    EXPECT_EQ(diff, static_cast<std::int16_t>(static_cast<std::uint16_t>(0x1000 - now)));
}

TEST(FacingDifferenceTest, DifferenceFromGivenFacingUsesArgument) {
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 30);
    const std::uint16_t now =
        static_cast<std::uint16_t>(ra2::runtime::facing_get_facing(f, 1015));
    const std::int16_t diff =
        ra2::runtime::facing_difference_from(f, 0x2000, 1015);
    EXPECT_EQ(diff, static_cast<std::int16_t>(static_cast<std::uint16_t>(0x2000 - now)));
}

// 旋转结束后 difference 归零（Value 已等于当前朝向）。
TEST(FacingDifferenceTest, ZeroAfterRotationCompletes) {
    FacingStruct f = Make(0x1000, 0x0000, 1, 1000, 30);
    EXPECT_EQ(ra2::runtime::facing_difference(f, 1100), 0);
}