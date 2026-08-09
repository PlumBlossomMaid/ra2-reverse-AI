// Randomizer(R250) 数值对拍测试
//
// 期望值全部来自原版 gamemd.exe 的机器码实跑（Unicorn 指令级模拟）：
//   生成命令: python code/randmap/emu_oracle.py 0 1 11584 60556 0xDEADBEEF ...
//   完整基准: memory/data/rmg/rng_golden.txt
// 只要这些数值对齐，"同一种子 -> 同一条随机序列"就成立，
// 这是随机地图字节级复刻的前提。
#include "randomizer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using ra2::rmg::Randomizer;

namespace {

std::vector<std::uint32_t> DrawRaw(std::uint32_t seed, int n) {
  Randomizer r(seed);
  std::vector<std::uint32_t> out;
  for (int i = 0; i < n; ++i) out.push_back(r.Random());
  return out;
}

std::vector<std::int32_t> DrawRanged(std::uint32_t seed, int lo, int hi, int n) {
  Randomizer r(seed);
  std::vector<std::int32_t> out;
  for (int i = 0; i < n; ++i) out.push_back(r.RandomRanged(lo, hi));
  return out;
}

}  // namespace

// 播种后的初始索引：0x65C6D5/0x65C6DD 写死 0 与 0x67
TEST(Randomizer, SeedInitialIndices) {
  Randomizer r(0);
  EXPECT_FALSE(r.disabled());
  EXPECT_EQ(0, r.index1());
  EXPECT_EQ(103, r.index2());
}

// 播种结果：4 轮 Feistel 混淆填满 250 个状态字（首尾各取 8 个校验）
TEST(Randomizer, SeedStateWords) {
  Randomizer r(0);
  const std::uint32_t head[8] = {0xAD2EAA18u, 0x667051BBu, 0xBEB8385Du, 0x1293A6D6u,
                                 0x9A1C341Du, 0x4868B99Cu, 0x803D17D0u, 0x569B65AFu};
  const std::uint32_t tail[8] = {0xCF34E2FCu, 0x44A6936Du, 0x26D1F7FEu, 0x6DABF2DFu,
                                 0x62FDBE0Fu, 0x203406B3u, 0xAD5EA053u, 0xD72E1536u};
  for (int i = 0; i < 8; ++i) EXPECT_EQ(head[i], r.state(i)) << "state[" << i << "]";
  for (int i = 0; i < 8; ++i) EXPECT_EQ(tail[i], r.state(242 + i)) << "state[" << (242 + i) << "]";

  Randomizer r2(60556);  // 实机 RandMap.Sed 里出现过的种子
  const std::uint32_t head2[8] = {0x11733A82u, 0xFC380E0Cu, 0x521FDB19u, 0xA2DD1150u,
                                  0x070396C8u, 0x4E79B005u, 0xE2EB4E22u, 0xB2E749B1u};
  for (int i = 0; i < 8; ++i) EXPECT_EQ(head2[i], r2.state(i)) << "state[" << i << "]";
}

// R250 取数序列
TEST(Randomizer, RandomSequence) {
  const std::uint32_t expect0[16] = {
      0x49466CF3u, 0x6A2B5556u, 0x9089E025u, 0x9CEFDB3Bu, 0x06C7FB77u, 0x5F4A416Du,
      0xF4AE1A0Eu, 0x3D672594u, 0xCA506A11u, 0xB0660A86u, 0xB5C05975u, 0xA0217290u,
      0x9DB8F04Au, 0xDD142E73u, 0xBD249013u, 0x9F73DB08u};
  const auto got0 = DrawRaw(0, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(expect0[i], got0[i]) << "random[" << i << "]";

  const std::uint32_t expect_seed1[16] = {
      0x78B76ED5u, 0x275D74AEu, 0xDA63B931u, 0xACEBBAD2u, 0xE74254D1u, 0x24B692A8u,
      0x236D1275u, 0x6D0091E1u, 0x71417079u, 0x16CDB40Au, 0x285FB0C5u, 0x31A86037u,
      0x5704CA5Fu, 0x770F459Cu, 0x0DA87494u, 0x5B69865Du};
  const auto got1 = DrawRaw(1, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(expect_seed1[i], got1[i]) << "random[" << i << "]";
}

// 索引回绕：250 次取数后双索引各自回到起点附近
TEST(Randomizer, IndexWrap) {
  Randomizer r(1);
  for (int i = 0; i < 250; ++i) r.Random();
  EXPECT_EQ(0, r.index1());
  EXPECT_EQ(103, r.index2());
}

// 区间随机：掩码 + 拒绝采样，闭区间
TEST(Randomizer, RandomRangedSequence) {
  const std::int32_t e99[16] = {86, 37, 59, 14, 20, 17, 6, 16, 74, 19, 8, 4, 79, 90, 68, 82};
  const auto g99 = DrawRanged(0, 0, 99, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(e99[i], g99[i]) << "ranged(0,99)[" << i << "]";

  const std::int32_t eneg[16] = {-2, 1, 0, 2, -1, -4, 1, 0, -5, 5, -2, -2, 3, -1, -2, 5};
  const auto gneg = DrawRanged(0, -5, 5, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(eneg[i], gneg[i]) << "ranged(-5,5)[" << i << "]";

  const std::int32_t e1k[16] = {244, 343, 38, 828, 888, 366, 527, 405,
                                530, 647, 374, 657, 75, 628, 20, 777};
  const auto g1k = DrawRanged(0, 1, 1000, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(e1k[i], g1k[i]) << "ranged(1,1000)[" << i << "]";

  // 种子 60556（实机参数）的区间序列
  const std::int32_t e60556[16] = {53, 89, 42, 44, 37, 0, 36, 48, 81, 21, 97, 27, 10, 13, 29, 23};
  const auto g60556 = DrawRanged(60556, 0, 99, 16);
  for (int i = 0; i < 16; ++i) EXPECT_EQ(e60556[i], g60556[i]) << "ranged60556[" << i << "]";
}

// 参数顺序无关（内部先排序），且 a==b 直接返回
TEST(Randomizer, RandomRangedEdges) {
  Randomizer r1(7);
  Randomizer r2(7);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(r1.RandomRanged(3, 3), 3);
    EXPECT_EQ(r1.RandomRanged(10, 20), r2.RandomRanged(20, 10));
  }
  // 结果必须落在闭区间内
  Randomizer r3(99);
  for (int i = 0; i < 2000; ++i) {
    const std::int32_t v = r3.RandomRanged(-7, 13);
    EXPECT_GE(v, -7);
    EXPECT_LE(v, 13);
  }
}

// disabled 时 Random 恒 0（0x65C780 首指令的短路分支）
TEST(Randomizer, DisabledShortCircuit) {
  Randomizer r(5);
  r.set_disabled(true);
  EXPECT_EQ(0u, r.Random());
  EXPECT_EQ(4, r.RandomRanged(4, 9));  // 抽到 0，加下界
}
