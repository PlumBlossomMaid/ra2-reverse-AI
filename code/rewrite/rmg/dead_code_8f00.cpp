// 死代码复现 —— 0x5A8F00 正态抖动取数（batch 929）
//
// 语义链见 dead_code_8f00.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 8 用例：deadcode_8f00_golden.txt）：
//   - mode<=1 捷径：edi=1（n=1）不经正态段直落 0x5A8FBE 表选择。
//   - 正态段：span/mid 默认 0.5/2.0；mode<1.5 时 mid=mode-2.0、span=mid*0.5
//     （golden 全 mode=4 走默认）。拒绝条件 v<2.0 或 v>mode（test ah,1 / 0x41；
//     v==mode 通过），_ftol（RC=chop）→ n。
//   - 表选择 eax=(A+4n)*4：表1[..]=P（id 数组）、表2[..]=条目数 ebp。
//   - n==2 分支（0x5A8FDD）：先 Random() 一次，再用
//     ftol(rand * (double)(uint64)(int32)(ebp-7) * 2^-32 + 7) 拒绝采样式取数
//     （原版怪癖①：无符号读 ebp-7，条目数<=7 死锁；golden 用 16）。
//   - 通用采样：ftol(rand * (double)(uint64)ebp * 2^-32)，v>ebp-1 重抽。
//   - 查找循环 ebx=0..ebp-1：P[(ebx+v)%ebp] → [0xABE370] 12B 项 → 子表扫描
//     找域==A（怪癖：count<=0 跳过、未命中越界读子表[count] 返回 0）；
//     → 坐标差 = B - 子表坐标（16 位减法）→ ProbeFamily 判定 → 成功出口。
//   - 失败重试 n'=(n+1)%(mode+1)（余 0 则 1），n'==初始 n 写 0xFFFF/0/-1。
#include "dead_code_8f00.h"

#include <cmath>
#include <cstdint>

namespace ra2 {
namespace rmg {

namespace {

// .rdata 取证（bytes 见 dead_code_8f00.h 头注释）：
constexpr double kTwo32Inv = 2.3283064370807974e-10;  // [0x7ED898] ≈ 2^-32
constexpr double k07 = 0.7;                            // [0x7ED838]
constexpr double k7 = 7.0;                             // [0x7E7FB0]
constexpr double k15 = 1.5;                            // [0x7E48F0]
constexpr double k20 = 2.0;                            // [0x7E1708]
constexpr double k05 = 0.5;                            // [0x7E1738]

inline int Ftol(double v) {
  // _ftol @0x7C5F00：FPCW=0x0E7F（RC=chop）→ 向零截断
  return static_cast<int>(v);
}

}  // namespace

int DeadCode8F00(DeadCode8F00Context& ctx, std::uint32_t* out2,
                 std::uint32_t* out3, std::uint32_t* out4) {
  Randomizer& rng = *ctx.rng;
  DeadCode8F00World& world = *ctx.world;
  const int mode = ctx.mode;
  const int A = ctx.A;
  const int bx = ctx.base.x;
  const int by = ctx.base.y;

  // ① 初始 n=1（[esp+0x10]/[esp+0x24]，edi）
  int n = 1;

  // ② 正态段（mode > 1 才走；mode<=1 捷径）
  if (mode > 1) {
    double mid = k20;
    double span = k05;
    if (mode < k15) {
      // fld [esp+0x24]=mode；fsub 2.0；fmul 0.5 → span=(mode-2)*0.5；
      // fadd 2.0 → mid=span+2.0
      span = (static_cast<double>(mode) - k20) * k05;
      mid = span + k20;
    }
    for (;;) {
      double v = world.NormalDeviate() * span + mid;
      if (v < k20) continue;                 // test ah,1 / jne 重采
      const double mode_v = static_cast<double>(mode);
      const bool le_mode = (v < mode_v) || (v == mode_v);
      if (!le_mode) continue;                // test ah,0x41 / je 重采（v>mode）
      n = Ftol(v);                           // 0x7C5F00
      break;
    }
  }

  // ⑤ 表选择 + ⑥ 查找循环（0x5A8FBE / 0x5A9030）
  const int initial_n = n;
  for (;;) {
    // eax = (n + 4A) * 4：双表按 idx = n + 4A 索引
    // （lea eax,[edi+eax*4]：edi=n、eax=A → n + 4*A，不是 A + 4*n）
    const int idx = n + 4 * A;
    const std::map<int, DualTableRow>::const_iterator it =
        ctx.table.find(idx);
    const DualTableRow* row = (it != ctx.table.end()) ? &it->second : nullptr;
    const int ebp = row ? row->ebp : 0;

    // n==2 特殊采样（0x5A8FDD）：r0 = Random()，r0/2^32 < 0.7 才走特殊路径
    int v = 0;
    if (n == 2) {
      const std::uint32_t r0 = rng.Random();
      const double f0 = static_cast<double>(r0) * kTwo32Inv;
      if (f0 < k07) {
        // lea esi,[ebp-1]；lea edx,[esi-6] → ebp-7；fild qword 无符号读
        const std::uint64_t u7 =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(ebp - 7));
        const double scale = static_cast<double>(u7);
        const std::uint32_t limit =
            static_cast<std::uint32_t>(ebp - 1);   // cmp eax,esi；ja 重抽
        for (;;) {
          const std::uint32_t r1 = rng.Random();
          v = Ftol(static_cast<double>(r1) * scale * kTwo32Inv + k7);
          if (static_cast<std::uint32_t>(v) <= limit) break;
        }
      } else {
        // f0 >= 0.7 → 0x5A9051 通用采样（fild qword 无符号读 ebp）
        const double scale = static_cast<double>(
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(ebp)));
        const std::uint32_t limit =
            static_cast<std::uint32_t>(ebp - 1);
        for (;;) {
          const std::uint32_t r2 = rng.Random();
          v = Ftol(static_cast<double>(r2) * scale * kTwo32Inv);
          if (static_cast<std::uint32_t>(v) <= limit) break;
        }
      }
    } else {
      // 通用采样（0x5A9051，n!=2 直接走）
      const double scale = static_cast<double>(
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(ebp)));
      const std::uint32_t limit =
          static_cast<std::uint32_t>(ebp - 1);
      for (;;) {
        const std::uint32_t r = rng.Random();
        v = Ftol(static_cast<double>(r) * scale * kTwo32Inv);
        if (static_cast<std::uint32_t>(v) <= limit) break;
      }
    }

    // 查找循环：ebx = 0..ebp-1
    for (int ebx = 0; ebx < ebp; ++ebx) {
      const int edx = (ebx + v) % ebp;         // idiv
      const int id = row->ids[static_cast<std::size_t>(edx)];
      const Abe8F00Entry& item = world.AbeEntry(id);
      const int count = item.count;
      // 子表扫描：找域 == A（count<=0 直落 esi=0）
      // 汇编 0x5A90C4-0x5A90D8：cmp [eax+4],A 命中即停（esi=命中下标），
      // 未命中 esi 逐步递增，循环结束 esi == count（越界下标，下方越界读=0）
      int esi = 0;
      if (count > 0) {
        for (esi = 0; esi < count; ++esi) {
          const std::size_t si = static_cast<std::size_t>(esi);
          if (si < item.sub.size() &&
              item.sub[si].second == A) {
            break;   // 命中：esi 停在命中下标
          }
        }
        // 未命中：esi == count（保留越界下标语义，坐标按 0 处理）
      }
      // 读子表[esi]（count<=0 时读 [0]，golden 进料保证子表指针非空）
      std::uint32_t coord = 0;
      {
        const std::size_t si = static_cast<std::size_t>(esi);
        if (si < item.sub.size()) {
          coord = item.sub[si].first;
        }
        // else：count<=0 且子表为空或越界 → 0（golden 观测值）
      }
      // 坐标差 = B - 子表坐标（16 位减法，ax/dx 截断）
      const int sx = static_cast<std::int16_t>(coord & 0xFFFF);
      const int sy = static_cast<std::int16_t>((coord >> 16) & 0xFFFF);
      const int dx = static_cast<std::int16_t>(bx - sx);
      const int dy = static_cast<std::int16_t>(by - sy);
      const std::uint32_t diff =
          (static_cast<std::uint32_t>(static_cast<std::uint16_t>(dy)) << 16) |
          static_cast<std::uint16_t>(dx);

      // 0x5A7D00（ecx=表族 id，edx=&坐标差，push 1,1）——怪癖②恒返 1
      if (world.ProbeFamily(item.family_id)) {
        if (out2) *out2 = static_cast<std::uint32_t>(item.family_id);
        if (out3) *out3 = diff;
        if (out4) *out4 = static_cast<std::uint32_t>(esi);
        return 1;                            // 0x5A9158
      }
      // 判定失败 → ebx++ 继续
    }

    // ⑦ 失败重试（0x5A90C8）：n'=(n+1)%(mode+1)，余 0 则 1
    int n_next = 0;
    if (mode + 1 != 0) {
      n_next = (n + 1) % (mode + 1);
    }
    if (n_next == 0) n_next = 1;
    if (n_next == initial_n) {
      if (out2) *out2 = 0xFFFF;
      if (out3) *out3 = 0;
      if (out4) *out4 = 0xFFFFFFFF;
      return 0;                              // 0x5A91A7
    }
    n = n_next;
    // jmp 0x5A8FBA：re-enter 表选择（不再走正态段）
  }
}

}  // namespace rmg
}  // namespace ra2