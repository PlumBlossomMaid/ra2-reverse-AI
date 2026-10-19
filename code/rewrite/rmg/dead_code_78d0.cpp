// 死代码复现 —— 0x5A78D0 随机抽样 + 小块批量放置（batch 931）
//
// 语义链见 dead_code_78d0.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 5 用例：deadcode_78d0_golden.txt）：
//   - 外层循环双计数：[esp+0x10] 成功放置轮数（上限 = limit/100，
//     0x51EB851F 有符号除法）、[esp+0x30] 总轮数（上限 = limit/100*10）。
//   - 每轮拒绝采样取模板项：ftol(rand * limit * 2^-32)（fild qword 无
//     符号读、fmul [0x7ED898]、ja 无符号重抽）→ [edi+4][采样]（dword）
//     → X=低16 有符号、Y=高16 有符号。
//   - 模板循环最多 5 项（edi=0..4）：每项 {X, Y} + 模板数组 [esp+0x60]
//     起 0x10 间隔（项 = {[ptr-4], [ptr]}）→ 组装 {X, Y, [ptr-4], [ptr]}
//     调 0x5A7B40（菱形校验打桩，azn_seq 序列）→ 成功 bl=1 提前退出。
//   - bl=0 全失败 → 跳过放置（只 [esp+0x30]++）。
//   - 成功：0..5 拒绝采样（fmul [0x7EDA40] = 6/2^32×(1+ε)，ja 重抽）→
//     宿主 = [0xAA10A4] + 采样 + 8 → 双重循环（外层 Y 上限 =
//     [esp+0x28]>>1 有符号 sar、内层 X 上限 = [esp+0x24]>>1）：
//     坐标 = {X+2*esi, Y+2*edi} → 0x5A6C10（打桩记录 ecx/edx/栈参）。
//   - 外层循环结束 → ret 4（返回 EAX 无意义，C++ 返回 0 对拍）。
#include "dead_code_78d0.h"

#include <cmath>
#include <cstdint>

namespace ra2 {
namespace rmg {
namespace {

// .rdata 取证（bytes 见头注释）：
constexpr double kTwo32Inv = 2.3283064370807974e-10;  // [0x7ED898] ≈ 2^-32
constexpr double kSixInv32 = 1.3969838622484784e-09;  // [0x7EDA40] ≈ 6/2^32

inline int Ftol(double v) { return static_cast<int>(v); }

// 0x51EB851F magic 配 sar edx,4（0x5A7964 imul + sar edx,4 + 符号校正）
// = 有符号除以 50！标准 MSVC /100 序列是 sar 5——这里是原版怪癖
// （golden 实证：limit=100 → ebp 必须 = 2 才能让 all-fail 跑 2 个成功轮）
inline int Div50(int v) {
  // edx = (v * 0x51EB851F) >> 32（64 位乘积高 32 位，有符号）
  const long long prod = static_cast<long long>(v) * 0x51EB851FLL;
  long long edx = prod >> 32;
  edx >>= 4;                 // sar edx, 4
  const long long sign = edx >> 31;   // shr eax,0x1f
  return static_cast<int>(edx + sign);
}

}  // namespace

int DeadCode78D0(const DeadCode78D0Context& ctx) {
  Randomizer& rng = *ctx.rng;
  DeadCode78D0World& world = *ctx.world;
  const int limit = ctx.host_limit;   // [edi+0x10]
  const std::vector<std::uint32_t>& arr = ctx.host_arr;  // [edi+4]

  // 模板数组（0x5A78E3-0x5A7A20 栈上初始化）：
  //   项 i（0..4）：pair {a=[ptr-4], b=[ptr]}，ptr = 0x60 + i*0x10
  //   a 序列：{6, 4, 4, 4, 2}；b 序列：{4, 6, 4, 2, 4}
  //   ★push edi 之前的 [esp+0x58]=6 物理落在 push 后的 [esp+0x5C]
  //   （= 项 0 的 a），[esp+0x5C]=4 落在 [esp+0x60]（= 项 0 的 b）——
  //   即项 0 是 {6,4} 不是 {4,0}（golden ZN d2 首值 6 实锤，曾误读）。
  //   （0x5A78E7 [0x5C]=4 / 0x5A7923 [0x60]=0 覆盖的是 [0x58]，
  //   0x5A7910 [0x6C]=4 / 0x5A78FA [0x70]=6、0x5A7914 [0x7C]=4 /
  //   0x5A7902 [0x80]=4、0x5A7918 [0x8C]=4 / 0x5A796B [0x90]=2、
  //   0x5A795D [0x9C]=2 / 0x5A797E [0xA0]=4）
  static const int kTmplA[5] = {6, 4, 4, 4, 2};
  static const int kTmplB[5] = {4, 6, 4, 2, 4};

  // 外层循环上限（0x5A7955-0x5A799F）：
  //   ebp = limit/50（Div50，sar edx,4 的 0x51EB851F magic 是 /50 不是
  //   /100——golden all-fail limit=100 → ebp=2 实锤）；esi = ebp*10 = 总轮上限
  const int ebp = Div50(limit);
  const int esi = ebp * 10;
  if (esi <= 0) return 0;

  int counter_success = 0;   // [esp+0x10]
  int counter_round = 0;     // [esp+0x30]

  for (;;) {
    // 0x5A79AE：成功计数 >= ebp → 提前退出
    if (counter_success >= ebp) break;
    // 0x5A7B26：总轮计数 >= esi → 退出
    if (counter_round >= esi) break;

    // 拒绝采样取模板项（0x5A79B8-0x5A79FE）：
    //   ebx = limit-1；do { r = Random(); v = ftol(r * limit * 2^-32);
    //        } while (v > limit-1)  （ja 无符号）
    int idx = 0;
    {
      const std::uint32_t upper = static_cast<std::uint32_t>(limit - 1);
      for (;;) {
        const std::uint32_t r = rng.Random();
        idx = Ftol(static_cast<double>(r) * limit * kTwo32Inv);
        if (static_cast<std::uint32_t>(idx) <= upper) break;
      }
    }
    // 模板项 = [edi+4][idx]（4B），X=低16 有符号、Y=高16 有符号
    const std::uint32_t item = arr[static_cast<std::size_t>(idx)];
    const int tx = static_cast<std::int16_t>(item & 0xFFFF);
    const int ty = static_cast<std::int16_t>((item >> 16) & 0xFFFF);

    // 模板循环（0x5A7A36-0x5A7A67）：edi=0..4 最多 5 项
    bool ok = false;
    int tmpl_a = 0, tmpl_b = 0;
    for (int i = 0; i < 5; ++i) {
      tmpl_a = kTmplA[i];   // [ptr-4]
      tmpl_b = kTmplB[i];   // [ptr]
      // 组装 {X, Y, a, b} → 0x5A7B40 菱形校验（打桩）
      if (world.ProbeArea(tx, ty, tmpl_a, tmpl_b)) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      ++counter_round;   // 0x5A7B1B：只总轮++，跳过放置
      continue;
    }

    // 成功：0..5 拒绝采样（0x5A7A71-0x5A7A99，fmul 6/2^32，ja 重抽）
    int off = 0;
    for (;;) {
      const std::uint32_t r = rng.Random();
      off = Ftol(static_cast<double>(r) * kSixInv32);
      if (static_cast<std::uint32_t>(off) <= 5u) break;
    }
    // 宿主 = [0xAA10A4] + 采样 + 8（0x5A7AA3 lea ebp,[eax+ecx+8]）
    const int host = ctx.ab10a4 + off + 8;

    // 双重循环（0x5A7AA7-0x5A7B08）：
    //   外层 Y 上限 = tmpl_b >> 1（有符号 sar）；内层 X 上限 = tmpl_a >> 1
    const int cy = tmpl_b / 2;   // sar eax,1（向零截断 = 算术右移）
    const int cx = tmpl_a / 2;
    if (cy > 0) {
      for (int j = 0; j < cy; ++j) {
        if (cx > 0) {
          for (int i = 0; i < cx; ++i) {
            // 坐标 = {X+2*esi, Y+2*edi} → 0x5A6C10（打桩）
            world.PlaceType(host, tx + 2 * i, ty + 2 * j);
          }
        }
      }
    }

    ++counter_success;   // 0x5A7B0E
    ++counter_round;     // 0x5A7B1B
  }

  // 0x5A7B2C：pop/add esp,0x94/ret 4
  return 0;
}

}  // namespace rmg
}  // namespace ra2