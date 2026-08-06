// 原版数学基元对拍：查表近似 sqrt + Marsaglia 极坐标正态分布
//
// FastSqrt / Uniform / NormalDeviate 的期望值由 code/randmap/dump_fastmath_golden.py
//（FPCW=0x0E7F），存档 memory/data/rmg/fastmath_golden.txt。
// 全部用位形比较：这些数值是地形形状的直接输入，差 1 ulp 就可能换一张地图。
#include <gtest/gtest.h>

#include <cfenv>
#include <cstring>
#include <vector>

#include "dd_log.h"
#include "fp_guard.h"
#include "fast_math.h"
#include "sqrt_table.h"
#include "scaled_random.h"

using ra2::rmg::FastSqrt;
using ra2::rmg::FpuChopScope;
using ra2::rmg::NormalDeviate;
using ra2::rmg::Randomizer;
using ra2::rmg::UniformUnit;

namespace {

// 逐位比较：浮点相等不足以说明复刻正确（-0.0 == 0.0，且容差会掩盖 1 ulp 偏差）
::testing::AssertionResult BitEq(double got, double want) {
  std::uint64_t a, b;
  std::memcpy(&a, &got, 8);
  std::memcpy(&b, &want, 8);
  if (a == b) return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "got " << got << " (" << std::hex << a << ") want " << std::dec << want
         << " (" << std::hex << b << ")";
}

}  // namespace

// 这条必须排在最前面：舍入模式若没生效，后面所有对拍都会以极难定位的方式失败
TEST(FpEnvironment, RoundingModeActuallyTakesEffect) {
  EXPECT_TRUE(ra2::rmg::FpuRoundingModeWorks())
      << "fesetround/_controlfp 未生效——检查是否启用了 -ffast-math / /fp:fast";
}

TEST(FpEnvironment, ChopScopeIsRestored) {
  const int before = std::fegetround();
  { ra2::rmg::FpuChopScope fpu; EXPECT_EQ(std::fegetround(), FE_TOWARDZERO); }
  EXPECT_EQ(std::fegetround(), before);
}

TEST(FastSqrt, MatchesOriginalLookupTable) {
  FpuChopScope fpu;
  static const double kCases[][2] = {
      {0, 0},
      {1, 1},
      {2, 1.4142135381698608},
      {3, 1.7320507764816284},
      {4, 2},
      {9, 3},
      {100, 10},
      {0.25, 0.5},
      {9.9999999999999995e-07, 0.0009999455651268363},
      {1000000000000, 999994.5625},
      {1234.5678, 35.135448455810547},
      {7, 2.6457512378692627},
      {16777216, 4096},
      {1.0000000000000001e-30, 9.9997131038640549e-16},
      {123456789, 11110.9599609375},
      {2.0000000999999998, 1.4142135381698608},
      {5, 2.2360677719116211},
      {6, 2.4494895935058594},
      {8, 2.8284270763397217},
      {10, 3.1622774600982666},
      {11, 3.316624641418457},
      {12, 3.4641015529632568},
      {13, 3.6055512428283691},
      {14, 3.7416572570800781},
      {15, 3.8729832172393799},
      {16, 4},
      {17, 4.123105525970459},
      {18, 4.242640495300293},
      {19, 4.3588986396789551},
      {20, 4.4721355438232422},
      {21, 4.5825753211975098},
      {22, 4.6904153823852539},
      {23, 4.7958312034606934},
      {24, 4.8989791870117188},
      {25, 5},
      {26, 5.0990190505981445},
      {27, 5.1961522102355957},
      {28, 5.2915024757385254},
      {29, 5.385164737701416},
      {30, 5.4772253036499023},
      {31, 5.5677642822265625},
      {32, 5.6568541526794434},
      {33, 5.7445626258850098},
      {34, 5.8309516906738281},
      {35, 5.9160795211791992},
      {36, 6},
      {37, 6.0827622413635254},
      {38, 6.1644139289855957},
      {39, 6.2449979782104492},
      {40, 6.3245549201965332},
      {41, 6.4031238555908203},
      {42, 6.4807405471801758},
      {43, 6.5574383735656738},
      {44, 6.6332492828369141},
      {45, 6.7082037925720215},
      {46, 6.7823295593261719},
      {47, 6.855654239654541},
      {48, 6.9282031059265137},
      {49, 7},
      {50, 7.0710678100585938},
      {51, 7.1414279937744141},
      {52, 7.2111024856567383},
      {53, 7.2801098823547363},
      {54, 7.3484687805175781},
      {55, 7.4161982536315918},
      {56, 7.4833145141601562},
      {57, 7.5498342514038086},
      {58, 7.6157727241516113},
      {59, 7.6811456680297852},
      {60, 7.7459664344787598},
      {61, 7.8102493286132812},
      {62, 7.8740077018737793},
      {63, 7.937253475189209},
      {64, 8},
  };
  for (const auto& c : kCases) EXPECT_TRUE(BitEq(FastSqrt(c[0]), c[1])) << "x=" << c[0];
}

// 写死的表必须等于纯整数重算的结果——证明它是数学常量表而非搬运的游戏数据，
// 同时保证任何架构上（x86 / ARM / LoongArch）拿到的都是同一张表
TEST(FastSqrt, TableIsPureMath) {
  auto isqrt = [](std::uint64_t n) {
    std::uint64_t root = 0, bit = std::uint64_t(1) << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
      if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
      else { root >>= 1; }
      bit >>= 2;
    }
    return root;
  };
  for (int i = 0; i < ra2::rmg::kSqrtTableSize; ++i) {
    const std::uint64_t m = std::uint64_t(i) * 1024;
    const std::uint64_t n = (m >= (std::uint64_t(1) << 23)) ? ((2 * m) << 23)
                                                            : (((std::uint64_t(1) << 23) + m) << 23);
    ASSERT_EQ(ra2::rmg::kSqrtTable[i], isqrt(n) - (std::uint64_t(1) << 23)) << "i=" << i;
  }
}

// ChopLog 的定义是「精确 ln 向零截断到 double」——这是个**与硬件无关的数学值**。
// 黄金值由 Python 的 Decimal(90 位)+Fraction 精确算出，不经过任何硬件浮点，
// 所以 x86 / ARM / 龙芯上都必须逐位相同。存档 memory/data/rmg/choplog_golden.txt。
TEST(ChopLog, MatchesArchIndependentGolden) {
  ra2::rmg::FpuChopScope fpu;
  static const std::uint64_t kGolden[][2] = {
    {0x3FDDA3E68E5DA3E6ull, 0xBFE8A1CB5EFFD639ull}, {0x3FE7BF831577BF83ull, 0xBFD3164283034376ull},
    {0x3FC745F7D89745F7ull, 0xBFFB468B8AAF16A5ull}, {0x3FEE1EC12CFE1EC1ull, 0xBFAEFF5CE080C120ull},
    {0x3FEB9E445D3B9E44ull, 0xBFC2D968E15C80CEull}, {0x3FE99C75E3799C75ull, 0xBFCC81AB22BB2407ull},
    {0x3FD195D4E05195D4ull, 0xBFF4AB4C6ADF9F03ull}, {0x3FC0B6721790B672ull, 0xC00049725AEF5341ull},
    {0x3FD0C5CFBA50C5CFull, 0xBFF56D1410A1B994ull}, {0x3FED739B4B7D739Bull, 0xBFB53EB49B8504B0ull},
    {0x3FC13EC748913EC7ull, 0xC00009348ED5D8A1ull}, {0x3FD43DB491D43DB4ull, 0xBFF26B324FFE0C08ull},
    {0x3FD1FDC4EE51FDC4ull, 0xBFF44DCE457E7DF4ull}, {0x3FCE7F4EC59E7F4Eull, 0xBFF6F34549885EAAull},
    {0x3FE87244EC387244ull, 0xBFD13B76D114DB5Bull}, {0x3FC92627CF992627ull, 0xBFFA090DB60D7C85ull},
    {0x3FEA15EE433A15EEull, 0xBFCA2823396FD2E1ull}, {0x3FEC35C93D7C35C9ull, 0xBFC0229864F7A350ull},
    {0x3FD3ABF53453ABF5ull, 0xBFF2E00EAFD5FB13ull}, {0x3FE22E11F4F22E11ull, 0xBFE217E326F720E7ull},
    {0x3FDBF1FA9A5BF1FAull, 0xBFEA8430CEF65185ull}, {0x3FD76F881ED76F88ull, 0xBFF012F06FB5F07Dull},
    {0x3FE9A07B5839A07Bull, 0xBFCC6D93ADA27633ull}, {0x3FDBED4CDBDBED4Cull, 0xBFEA898CCA56E18Eull},
    {0x3FD3E23F8A53E23Full, 0xBFF2B422F6AA6623ull}, {0x3FEDC3186F7DC318ull, 0xBFB28F6316112055ull},
    {0x3FC8452898984528ull, 0xBFFA9AC2623BC014ull}, {0x3FAA6B760E1A6B76ull, 0xC007B6B4CB87D3CBull},
    {0x3FCD4D702C9D4D70ull, 0xBFF796F81167EF6Dull}, {0x3FEB9F64197B9F64ull, 0xBFC2D433713CA72Aull},
    {0x3FE2D4926432D492ull, 0xBFE0F7EF5EDE6D91ull}, {0x3FD0815D9FD0815Dull, 0xBFF5AEE59933993Full},
    {0x3FE0EC2BCF30EC2Bull, 0xBFE46308F7425A01ull}, {0x3FD7FBBE3AD7FBBEull, 0xBFEF68A18F60C874ull},
    {0x3FE5C3274E35C327ull, 0xBFD8ACF2CFA914CAull}, {0x3FE2FAC734F2FAC7ull, 0xBFE0B74389597F0Cull},
    {0x3FE7CB304537CB30ull, 0xBFD2F6D285354A14ull}, {0x3FE390EAB37390EAull, 0xBFDF7BEB21C0A9C2ull},
    {0x3FD73B4A00573B4Aull, 0xBFF036C333797D96ull}, {0x3FE2D536CDF2D536ull, 0xBFE0F6D7FD023495ull},
    {0x01A56E1FC2F8F359ull, 0xC085963447F87FB5ull}, {0x2B2BFF2EE48E0530ull, 0xC06CC845B54B54F1ull},
    {0x3BC79CA10C924223ull, 0xC047069E2AA2AA5Aull}, {0x3E112E0BE826D695ull, 0xC034B927F32BFFB8ull},
    {0x3EE4F8B588E368F1ull, 0xC027069E2AA2AA5Aull}, {0x3F50624DD2F1A9FCull, 0xC01BA18A998FFFA0ull},
    {0x3F847AE147AE147Bull, 0xC0126BB1BBB55515ull}, {0x3FB999999999999Aull, 0xC0026BB1BBB55515ull},
    {0x3FD0000000000000ull, 0xBFF62E42FEFA39EFull}, {0x3FE0000000000000ull, 0xBFE62E42FEFA39EFull},
    {0x3FE8000000000000ull, 0xBFD269621134DB92ull}, {0x3FECCCCCCCCCCCCDull, 0xBFBAF8E8210A415Bull},
    {0x3FEFAE147AE147AEull, 0xBF8495453E6FD4BBull}, {0x3FEFF7CED916872Bull, 0xBF5064670D979B73ull},
    {0x3FEFFF2E48E8A71Eull, 0xBF1A3738D2CF1CC1ull}, {0x3FEFFFEB074A771Dull, 0xBEE4F8BC681DF714ull},
    {0x3FEFFFFDE7210BE9ull, 0xBEB0C6F82D74D22Full}, {0x3FEFFFFFFFFFFFFEull, 0xBCB0000000000000ull},
    {0x0000000000000001ull, 0xC0874385446D71C3ull}, {0x0010000000000000ull, 0xC086232BDD7ABCD2ull},
    {0x3FDFFFFFFFFFFFFEull, 0xBFE62E42FEFA39F1ull}, {0x3FD762348A3BA892ull, 0xBFF01C0C2E5C9D51ull},
    {0x3FDA9A005AD672DAull, 0xBFEC17DCD8C9A1EAull}, {0x3F99A8124D04A900ull, 0xC00D7E4EC9DF7504ull},
    {0x3FE6D42D328C8A64ull, 0xBFD59D1451A8AE8Eull}, {0x3FED0A4A0B97589Aull, 0xBFB8D89A337F1B2Full},
    {0x3FE8BBE94F1D6966ull, 0xBFD07BCBD83A6A2Dull}, {0x3FE2BE4EE79D0A62ull, 0xBFE11DDB6B56C916ull},
    {0x3FECEFD0A4258FDBull, 0xBFB9C266262796AFull}, {0x3FE58A0B71D9F96Aull, 0xBFD955C3B47F46A4ull},
    {0x3FEB832CAF3FEE0Cull, 0xBFC35736DCF8801Cull}, {0x3FECF27C81177EC0ull, 0xBFB9AAC53096F9C4ull},
    {0x3FDB1190E3A72684ull, 0xBFEB894837133736ull}, {0x3FE0EB2F82DD4A57ull, 0xBFE464E61BA85DA5ull},
    {0x3FD10AE88ECD1398ull, 0xBFF52BB078A74B9Cull}, {0x3FA6413D0ABC7E60ull, 0xC009160C12E38F71ull},
    {0x3FD525ED25B0F4BAull, 0xBFF1B7A05AB6B87Eull}, {0x3FE975AA18AB3980ull, 0xBFCD4423DEE093C7ull},
    {0x3FD808F3B9C050D6ull, 0xBFEF57069E9267C4ull}, {0x3FE12AC692E84EAEull, 0xBFE3ED7FBF2D9897ull},
    {0x3FBDCAC163B817A8ull, 0xC001351E81D1A2FFull}, {0x3FE85677C1221FBAull, 0xBFD18468E73F0A71ull},
    {0x3FA80786B32A3BC0ull, 0xC00878ED6A0607EAull}, {0x3FD42F946E7CEADCull, 0xBFF27660AD2D6724ull},
    {0x3F84BB10DF3DA440ull, 0xC0125F3BEC863040ull}, {0x3FD1835953553258ull, 0xBFF4BC264A192F96ull},
    {0x3FE71B2B28EC5975ull, 0xBFD4D7410DC3B58Cull}, {0x3FEFAD33B79EB4A8ull, 0xBF84CE08258941BFull},
    {0x3FEEB5D5C8D92AF2ull, 0xBFA5100AC5990617ull}, {0x3FA53168FC797470ull, 0xC0097A2B8243FAAEull},
    {0x3FC5E11488C3D068ull, 0xBFFC438E1DFA279Dull}, {0x3FD89B2261D86E86ull, 0xBFEE96ACFE66D9EDull},
    {0x3FE0990D70E5AAD1ull, 0xBFE501BCB3BA89C0ull}, {0x3FE663D0D0BEB107ull, 0xBFD6DB25CFD2F912ull},
    {0x3FCA70B487CECD48ull, 0xBFF93BF9E57F9902ull}, {0x3FE9C8E6B0975640ull, 0xBFCBA44FC7E17EA6ull},
    {0x3FDC7BF3B9091EFEull, 0xBFE9E7B3C824CA5Bull}, {0x3FD192A65EC5BF28ull, 0xBFF4AE31C26493B9ull},
    {0x3FEA3C406C851B8Bull, 0xBFC96CA33E9198FDull}, {0x3FC64BD5B5B2F498ull, 0xBFFBF638B45BBE30ull},
    {0x3FD0C0619F508FE4ull, 0xBFF57242F80B68B9ull},
  };
  for (const auto& c : kGolden) {
    double s, want;
    std::memcpy(&s, &c[0], 8);
    std::memcpy(&want, &c[1], 8);
    EXPECT_TRUE(BitEq(ra2::rmg::ChopLog(s), want)) << "s bits=" << std::hex << c[0];
  }
}

#if defined(_M_IX86) || defined(__i386__)
// 真 CPU 的 fyl2x 并不是正确舍入的：20000 例实测有 7 例（0.035 个百分点）比精确截断
// 低 1 ulp，全部出现在精确值距离截断边界不到 0.4 个 80 位 ulp 的地方。
// 因此本测试**不要求**逐位相同，只钉死两件事：
//   1. 偏差方向与幅度受控（只会低 1 ulp，绝不会高）；
//   2. 这 1 ulp 不会传到下游——唯一消费者 FastSqrt 只取尾数高 14 位。
// 若哪天某台机器上偏差超出此界，这条会立刻炸出来。
TEST(ChopLog, X87DeviatesOnlyWhereItIsNotCorrectlyRounded) {
  ra2::rmg::FpuChopScope fpu;
  ra2::rmg::Randomizer rng(20260806u);
  int diff_ln = 0, diff_sqrt = 0, total = 0;
  for (int i = 0; i < 20000; ++i) {
    double s;
    do { s = ra2::rmg::UniformUnit(rng); } while (s <= 0.0 || s >= 1.0);
    ++total;
    const double a = ra2::rmg::ChopLog(s);
    const double b = ra2::rmg::ChopLogX87(s);
    if (a == b) continue;
    ++diff_ln;
    std::uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    // 二者皆为负；位形小 1 表示幅度小 1 ulp，即 fyl2x 又向零多截了一步
    EXPECT_LT(a, 0.0);
    EXPECT_EQ(bb, ba - 1) << "s=" << s << " 偏差超出「低 1 ulp」";
    if (FastSqrt((-a - a) / s) != FastSqrt((-b - b) / s)) ++diff_sqrt;
  }
  EXPECT_LT(diff_ln, total / 100) << "fyl2x 分歧率异常升高：" << diff_ln << "/" << total;
  EXPECT_EQ(diff_sqrt, 0) << "1 ulp 的 ln 偏差穿透了 FastSqrt——正态偏差不再可复刻";
}
#endif

// 查表只有 14 位精度，与 std::sqrt 系统性不等——这条断言防止有人"优化"掉它
TEST(FastSqrt, DeliberatelyDiffersFromStdSqrt) {
  FpuChopScope fpu;
  EXPECT_NE(FastSqrt(2.0), 1.4142135623730951);
  EXPECT_NEAR(FastSqrt(2.0), 1.4142135623730951, 1e-6);
}

TEST(Uniform, MatchesOriginal) {
  FpuChopScope fpu;
  {
    Randomizer rng(0u);
    static const double kWant[] = {0.28623085918050045, 0.41472371444448913, 0.56460381428818307, 0.61303491788288456, 0.026488987502290163, 0.37222680015774134, 0.95578158249980338, 0.23985514516007506};
    for (double want : kWant) EXPECT_TRUE(BitEq(UniformUnit(rng), want));
  }
  {
    Randomizer rng(1u);
    static const double kWant[] = {0.4715489646120809, 0.15376977206994075, 0.8530841585837966, 0.67547195094532142, 0.90335588574021952, 0.14341084103645077, 0.13838305816482357, 0.42578994515952417};
    for (double want : kWant) EXPECT_TRUE(BitEq(UniformUnit(rng), want));
  }
  {
    Randomizer rng(11584u);
    static const double kWant[] = {0.99793717102099599, 0.34495545419514073, 0.61292221201884611, 0.81766229514443833, 0.38716878238766655, 0.32817332058403947, 0.54432919587575113, 0.83878296377108963};
    for (double want : kWant) EXPECT_TRUE(BitEq(UniformUnit(rng), want));
  }
  {
    Randomizer rng(60556u);
    static const double kWant[] = {0.62963954862897265, 0.24612402013645598, 0.41491721067924914, 0.45895878515647692, 0.045623400492040299, 0.79605087656435802, 0.6388977851343568, 0.57362608252410452};
    for (double want : kWant) EXPECT_TRUE(BitEq(UniformUnit(rng), want));
  }
  {
    Randomizer rng(3735928559u);
    static const double kWant[] = {0.74502870737226412, 0.88113298590321387, 0.29142767546964521, 0.77832218184562441, 0.69656208080625204, 0.49067568371320969, 0.060463263667296444, 0.50143411906935131};
    for (double want : kWant) EXPECT_TRUE(BitEq(UniformUnit(rng), want));
  }
}

TEST(NormalDeviate, MatchesOriginal) {
  FpuChopScope fpu;
  {
    Randomizer rng(0u);
    NormalDeviate gauss(rng);
    static const double kWant[] = {
        -1.6362744840891681, -0.65273878922591433, 1.1511449467403259, 2.0141159768935024,
        -0.26818046141956825, -0.072366375409098571, 1.0151624659018239, 0.66114543516904745,
        1.4514297647883145, 0.86761309366180772, 0.31614517124340868, 0.99004143212313733,
        1.4019884264730564, 0.72119872679433128, -0.94371913235123583, 0.002847572934107442,
    };
    for (double want : kWant) EXPECT_TRUE(BitEq(gauss(), want));
  }
  {
    Randomizer rng(1u);
    NormalDeviate gauss(rng);
    static const double kWant[] = {
        -0.09883759390139564, -1.2027879547423996, 0.87289774221570748, 0.43380329045831489,
        -1.0791265872919411, -0.22145545177264278, -0.11987600454929517, -0.85527738834118605,
        -0.43530196199358134, -0.38918206563455177, -2.0638856188689414, -0.45025056264836116,
        -0.482250186596409, -0.1543131543728696, -0.062876457338276806, -1.1074859953274165,
    };
    for (double want : kWant) EXPECT_TRUE(BitEq(gauss(), want));
  }
  {
    Randomizer rng(11584u);
    NormalDeviate gauss(rng);
    static const double kWant[] = {
        0.42053693402866138, 1.1830155048171973, -1.0349803522301031, -1.5761350532923848,
        0.16011309165694565, 1.223653771706795, -1.9920043743520359, 0.93113480494739975,
        -0.1383072129285022, -0.57202881277413242, 0.19715020962940688, 1.0250731970406581,
        -0.7210867215367841, 1.286697055602257, -0.5621316947741305, -1.5137567642708418,
    };
    for (double want : kWant) EXPECT_TRUE(BitEq(gauss(), want));
  }
  {
    Randomizer rng(60556u);
    NormalDeviate gauss(rng);
    static const double kWant[] = {
        0.68178821048881488, -1.3351608504333909, -2.3253259328132541, -1.1216628174945011,
        1.9007471020308524, 1.0075363178484573, -0.13194752289534575, 0.52149208988964302,
        0.013228668317615936, 0.79223046528879182, 1.324655499383032, -1.3358657361104889,
        0.051784878719881723, -0.77591917214949324, 0.024638434758608219, -1.2580721385180993,
    };
    for (double want : kWant) EXPECT_TRUE(BitEq(gauss(), want));
  }
  {
    Randomizer rng(3735928559u);
    NormalDeviate gauss(rng);
    static const double kWant[] = {
        0.3394284637254214, 0.52796827468736929, -0.72257449123615314, 0.96421473654127876,
        1.9291174606700552, -0.091511553418014244, -0.71796852647855669, 0.002342585431215414,
        -0.081485994349308019, 1.8523575874090992, 0.0018539150869115683, 0.74295064295505941,
        0.92432079626118535, 0.41444409375325503, -0.26144951570620478, -0.11699633582440055,
    };
    for (double want : kWant) EXPECT_TRUE(BitEq(gauss(), want));
  }
}

// 缓存的第二个偏差决定随机数流的消耗节奏，漏掉就会整张地图偏移
TEST(NormalDeviate, CachesSecondValue) {
  FpuChopScope fpu;
  Randomizer rng(11584u);
  NormalDeviate gauss(rng);
  EXPECT_FALSE(gauss.has_cached());
  gauss();
  EXPECT_TRUE(gauss.has_cached());
  gauss();
  EXPECT_FALSE(gauss.has_cached());
}

