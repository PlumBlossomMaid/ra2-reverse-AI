// RA2/YR 随机地图生成器 —— 随机数发生器（R250）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：
//   Randomizer::Randomizer   @ 0x65C6D0   播种：4 轮 Feistel 混淆填充 250 状态字
//   Randomizer::Random       @ 0x65C780   R250 取数
//   Randomizer::RandomRanged @ 0x65C7E0   区间随机（掩码 + 拒绝采样）
// 取证：memory/data/decomp/randomizer_asm.txt（逐条汇编）
//
// 这是"同一种子 -> 同一张地图"的地基：随机序列必须逐个数值与原版一致，
// 任何一次多余/缺失的取数都会让后续地形全盘偏移。
#pragma once

#include <cstdint>

namespace ra2::rmg {

// 对应原版对象布局（0x3F4 字节）:
//   +0x00 char  Disabled   非 0 时 Random() 恒返回 0
//   +0x04 int   Index1     初值 0
//   +0x08 int   Index2     初值 0x67 (103)
//   +0x0C uint  State[250]
class Randomizer {
 public:
  static constexpr int kStateSize = 250;   // 0xFA
  static constexpr int kLag = 103;         // 0x67

  Randomizer() = default;
  explicit Randomizer(std::uint32_t seed) { Seed(seed); }

  // 0x65C6D0：由种子派生 250 个状态字
  void Seed(std::uint32_t seed);

  // 0x65C780：state[i1] ^= state[i2]，返回异或后的新值，双索引各自 +1 并在 250 处回绕
  std::uint32_t Random();

  // 0x65C7E0：返回 [min(a,b), max(a,b)] 闭区间内的值
  std::int32_t RandomRanged(std::int32_t a, std::int32_t b);

  void set_disabled(bool v) { disabled_ = v; }
  bool disabled() const { return disabled_; }
  int index1() const { return index1_; }
  int index2() const { return index2_; }
  std::uint32_t state(int i) const { return state_[i]; }

  // 取数次数。原版没有这个字段——它是复刻侧的观测点：
  // 每个阶段消耗多少随机数是可以独立于结果对拍的一条证据链
  // （见 docs/random-map/rng-ledger.md），顺序对而次数不对，
  // 说明有一路分支白抽了随机数。
  long long call_count() const { return call_count_; }
  void reset_call_count() { call_count_ = 0; }

 private:
  bool disabled_ = false;
  std::int32_t index1_ = 0;
  std::int32_t index2_ = kLag;
  std::uint32_t state_[kStateSize] = {};
  long long call_count_ = 0;
};

}  // namespace ra2::rmg
