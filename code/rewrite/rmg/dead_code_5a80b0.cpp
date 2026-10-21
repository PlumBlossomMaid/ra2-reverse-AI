#include "dead_code_5a80b0.h"

#include <cstdint>

#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

// 0x5A80B0（546B，thiscall ecx=this ret 0——编排树根 B）。
// 完整语义见头文件注释；此处按反汇编全 0x222=546B 逐条复刻：
//   A. 数组初始化两循环（12B 步长写首 dword = 源+i）
//   B. 内层上限 = _ftol((double)this60 + 0.49) * 2
//      （fild dword → fadd [0x7EDB10]=0.49 → fadd st0,st0 = ×2 → _ftol）
//   C. 外层循环：[esp+0x2C]<10 且 [esp+0x18]<上限
//   D. 内层选点：PickBelowMul(side²) 形态（v=_ftol(Random()*side²*kInv)
//      拒绝 ja 无符号 v>side²-1）→ work[v] 判空重抽 → 成功计数
//      [esp+0x14]（<20 时邻域检查）→ ebp>200 强制 (0,0) → 0x486380
//      谓词失败重选（ebp 保留）
//   E. 邻域检查：work[y*side+x].+0x45 != 0 → 重选（ebp 清零）；
//      == 0 → 菱形校验 0x5A7250(x-4,y-4,8,8,0,0)（失败重选 ebp 清零）
//      → 0x578460 IsWithinUsableArea(&coord,1)（失败重选）
//   F. 成功点==20 → 跳过三连调；否则无条件三连调（★无概率判定）：
//      0x5A89D0（al!=0 继续）→ 0x5A82E0 → 0x5A8720 → inner_done++
void DeadCode5A80B0(DeadCode5A80B0Context& ctx) {
  // A. 数组初始化两循环（0x5A80C7-0x5A80F7，与根 A 相同）
  for (std::uint32_t i = 0; i < ctx.arr1.size(); ++i) {
    ctx.arr1[i] = ctx.src1 + i;  // [0xABE378+i*12] = [0xABC1D4]+i
  }
  for (std::uint32_t i = 0; i < ctx.arr2.size(); ++i) {
    ctx.arr2[i] = ctx.src2 + i;  // [0xABE834+i*12] = [0xABBEC0]+i
  }

  // B. 内层完成数上限 [esp+0x30]
  //    fild dword [this+0x60] → fadd 0.49 → fadd st0,st0（×2）→ _ftol
  const double limit_d = ((static_cast<double>(ctx.this60) + 0.49) * 2.0);
  const std::int32_t limit = Ftol(limit_d);

  // C. 外层循环（[esp+0x2C] 轮次 + [esp+0x18] 内层完成数）
  std::int32_t inner_done = 0;  // [esp+0x18]
  std::int32_t outer = 0;       // [esp+0x2C]
  const std::int32_t side2 = ctx.side * ctx.side;

  while (inner_done < limit && outer < 10) {
    // 内层（0x5A8133 起）
    std::int32_t success = 0;  // [esp+0x14] 成功点计数（上限 20）
    std::int32_t ebp = 0;      // 尝试计数（>200 强制 (0,0)）
    std::uint32_t coord = 0;   // [esp+0x10] 选中点坐标打包
    bool got = false;

    while (!got) {
      // D. 取数（0x5A8143-0x5A817F）：PickBelowMul(side²) 形态
      //    v = _ftol((double)(uint32)Random() * (double)side² * kInvUintMax)
      //    拒绝条件 ja 无符号 v > side²-1（★非根 A 的 PickRange！）
      std::int32_t v;
      do {
        const std::uint32_t r = ctx.rng->Random();
        v = Ftol(static_cast<double>(r) * static_cast<double>(side2) *
                 kInvUintMax);
      } while (static_cast<std::uint32_t>(v) >
               static_cast<std::uint32_t>(side2 - 1));
      // work[v] 判空（0x50E470：work[v].xy==(0,0) → 重抽）
      if (ctx.world->WorkEmpty(static_cast<std::uint32_t>(v))) {
        continue;  // jne 0x5A8143
      }
      ++ebp;  // 0x5A81AF inc ebp
      // work[v] 坐标打包（0x5A81B0 mov eax,[edx+eax]）
      coord = ctx.world->WorkCoord(static_cast<std::uint32_t>(v));
      if (ebp > 200) {
        // 0x5A81E4：强制 (0,0)
        coord = 0;
        got = true;
        break;
      }
      // GetCellAt → 0x486380 谓词（失败 → 重选，ebp 保留）
      const int x = static_cast<std::int16_t>(coord & 0xFFFF);
      const int y = static_cast<std::int16_t>(coord >> 16);
      if (!ctx.world->CellPass(x, y)) {
        continue;  // jmp 0x5A813B（ebp 保留）
      }
      got = true;
    }

    // 0x5A81F8：成功点计数 +1；[esp+0x10] = coord
    ++success;  // [esp+0x14]
    // 0x5A820C：计数 >= 20 → 跳过邻域检查直接三连调判定
    if (success < 20) {
      const int x = static_cast<std::int16_t>(coord & 0xFFFF);
      const int y = static_cast<std::int16_t>(coord >> 16);
      const std::uint32_t idx = static_cast<std::uint32_t>(y * ctx.side + x);
      // 0x5A822F：work[idx].+0x45 != 0 → 重选（ebp 清零）
      if (ctx.world->WorkFlag45(idx)) {
        ebp = 0;
        continue;  // jne 0x5A8139
      }
      // 0x5A8239-0x5A8262：菱形校验 0x5A7250(x-4, y-4, 8, 8, 0, 0)
      //   ★push ebx 位移：lea ecx,[esp+0x50] 在 push 后执行，实际指
      //   push 前 [esp+0x4C]——rect{+0=X-4/+4=Y-4/+8=8/+0xC=8} 对位
      if (!ctx.world->DiamondPass(x - 4, y - 4)) {
        ebp = 0;
        continue;  // je 0x5A8133（重载 side + xor ebp）
      }
      // 0x5A8268-0x5A827B：0x578460 IsWithinUsableArea(&coord, 1)
      if (!ctx.world->UsablePass(x, y)) {
        ebp = 0;
        continue;  // je 0x5A8133
      }
    }

    // F. 0x5A8281：成功点==20 → 跳过三连调
    if (success == 20) {
      // je 0x5A82B8（外层尾）
    } else {
      // 无条件三连调（★根 A 此处有 Random 概率门，根 B 没有）
      if (ctx.world->Sub5A89D0()) {  // al!=0 继续
        ctx.world->Sub5A82E0();
        ctx.world->Sub5A8720();
        ++inner_done;  // inc [esp+0x18]
      }
      // al==0 → 跳过（je 0x5A82B8）
    }
    ++outer;  // 0x5A82B8 inc [esp+0x2C]（无条件）
    // 0x5A82C4：jl 0x5A8115（外层循环头）
  }
}

}  // namespace rmg
}  // namespace ra2