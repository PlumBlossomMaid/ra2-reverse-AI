#include "randomizer.h"

namespace ra2::rmg {
namespace {

// 播种混淆用的两组轮常量。
// 取证：从 gamemd.exe .data 直接读原始字节（code/randmap/read_rng_keys.py）
//   KEY1 @ VA 0x839644，KEY2 @ VA 0x839694，各取前 4 个 dword（4 轮）。
// 原表每组各 20 个 dword，本算法只用到前 4 个（内层循环 CMP EBX,0x10 -> 4 轮）。
constexpr std::uint32_t kKey1[4] = {
    0xBAA96887u, 0x1E17D32Cu, 0x03BCDC3Cu, 0x0F33D1B2u};
constexpr std::uint32_t kKey2[4] = {
    0x4B0F3B58u, 0xE874F0C3u, 0x6955C5A6u, 0x55A7CA46u};

}  // namespace

// 0x65C6D0
// 每个状态字由 (种子, 下标) 经 4 轮 Feistel 混淆得到：
//   L = seed, R = i
//   每轮: x = KEY1[r] ^ R; hi = (int32)x >> 16; lo = x & 0xFFFF
//         t = ~(hi*hi) + lo*lo
//         m = ((uint32)(t >> 16)) | ((uint32)t << 16)      <- 见下方符号性说明
//         newR = ((m ^ KEY2[r]) + hi*lo) ^ L;  L = R;  R = newR
void Randomizer::Seed(std::uint32_t seed) {
  index1_ = 0;
  index2_ = kLag;

  for (int i = 0; i < kStateSize; ++i) {
    std::uint32_t left = seed;
    std::uint32_t right = static_cast<std::uint32_t>(i);

    for (int r = 0; r < 4; ++r) {
      const std::uint32_t x = kKey1[r] ^ right;

      // 0x65C718 SAR EDX,0x10 —— 算术右移，负数补 1（不是逻辑右移）
      const std::int32_t hi = static_cast<std::int32_t>(x) >> 16;
      const std::uint32_t lo = x & 0xFFFFu;

      const std::uint32_t hi_u = static_cast<std::uint32_t>(hi);
      // 0x65C72E NOT EDX / 0x65C730 ADD EDX,ECX
      const std::int32_t t =
          static_cast<std::int32_t>(~(hi_u * hi_u) + lo * lo);

      // 0x65C734 SAR ECX,0x10 / 0x65C737 SHL EDX,0x10 / 0x65C73A OR ECX,EDX
      // 注意：这不是纯粹的 16 位循环移位。SAR 会把符号位铺满高 16 位，
      // 随后与 (t << 16) 做 OR —— t 为负时高半字被污染成 0xFFFF。
      // 反编译写成 `t >> 16 | t * 0x10000` 是对的，但极易被误当作 ROR16 实现。
      const std::uint32_t mixed =
          static_cast<std::uint32_t>(t >> 16) | (static_cast<std::uint32_t>(t) << 16);

      // 0x65C742 XOR / 0x65C744 ADD EDI(hi*lo) / 0x65C746 XOR EBP(L)
      const std::uint32_t next = ((mixed ^ kKey2[r]) + hi_u * lo) ^ left;

      left = right;
      right = next;
    }

    state_[i] = right;
  }

  disabled_ = false;  // 0x65C770 MOV byte ptr [EAX],0x0
}

// 0x65C780
std::uint32_t Randomizer::Random() {
  if (disabled_) {
    return 0;
  }

  state_[index1_] ^= state_[index2_];
  const std::uint32_t result = state_[index1_];

  ++index1_;
  ++index2_;
  if (index1_ >= kStateSize) {
    index1_ = 0;
  }
  if (index2_ >= kStateSize) {
    index2_ = 0;
  }
  return result;
}

// 0x65C7E0
std::int32_t Randomizer::RandomRanged(std::int32_t a, std::int32_t b) {
  if (a == b) {
    return a;  // 0x65C7EF JZ -> 直接返回下界
  }

  std::int32_t lower = a;
  std::int32_t upper = b;
  if (a > b) {
    lower = b;
    upper = a;
  }

  const std::int32_t range = upper - lower;

  // 求覆盖 range 所需的位数：从第 30 位向下找最高置位（第 31 位单独判）
  int bits;
  if (static_cast<std::uint32_t>(range) & 0x80000000u) {
    bits = 32;  // 0x65C801 TEST EDI,0x80000000 -> 跳过搜索，ECX 保持 0x1F
  } else {
    int c = 31;
    while (c > 0) {
      --c;
      if (static_cast<std::uint32_t>(range) & (1u << c)) {
        break;
      }
    }
    bits = c + 1;  // 0x65C81E INC ECX
  }

  // 0x65C81F OR EBP,-1 / SHL EBP,CL / NOT EBP
  // x86 移位取 CL 低 5 位：bits==32 时 SHL 实际不移动，掩码退化为 0（原版行为，如实保留）
  const std::uint32_t mask = ~(0xFFFFFFFFu << (bits & 31));

  // 0x65C824 LEA EAX,[EDI+1] / CMP EAX,EDI / JLE —— range+1 溢出时不做采样
  if (range + 1 <= range) {
    return (range + 1) + lower;
  }

  // 拒绝采样：取够位数后掩码，超出 range 就重抽（消除取模偏差）
  std::uint32_t result;
  for (;;) {
    const std::uint32_t draw = disabled_ ? 0u : Random();
    result = draw & mask;
    if (static_cast<std::int32_t>(result) <= range) {  // 0x65C882 JG 有符号比较
      break;
    }
  }

  return static_cast<std::int32_t>(result) + lower;  // 0x65C886 ADD EAX,EBX
}

}  // namespace ra2::rmg
