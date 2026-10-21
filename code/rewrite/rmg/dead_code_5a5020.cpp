#include "dead_code_5a5020.h"

#include <cstdint>
#include <cstring>

#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

// 0x5A5020（827B，thiscall ecx=this ret 0——编排树根 A）。
// 完整语义见头文件注释；此处按反汇编四段逐条复刻：
//   A. 数组初始化两循环（12B 步长写首 dword = 源+i）
//   B. 内层上限 = _ftol((double)this60+0.32)*3.0（fild dword 有符号）
//   C. 外层循环：[esp+0x24]<10 且 [esp+0x18]<上限
//   D. 内层选点：PickRange(0,side²-1) → work[v] 判空重抽 → 成功计数
//      [esp+0x20]（<20 时邻域扫描）→ 概率判定
//   E. 概率判定：1.0-this60 > r·kInvUintMax → 三连调；否则 magic
//   F. magic：定标 + NormalDeviate 拒绝循环 + _ftol + 五连调 + 虚调用
//      ★0x5A5333 push 1 后写 [esp+0x1c] 实为 [esp+0x18]（内层完成数+1）
void DeadCode5A5020(DeadCode5A5020Context& ctx) {
  // A. 数组初始化两循环（0x5A505B-0x5A508D）
  for (std::uint32_t i = 0; i < ctx.arr1.size(); ++i) {
    ctx.arr1[i] = ctx.src1 + i;  // [0xABE378+i*12] = [0xABC1D4]+i
  }
  for (std::uint32_t i = 0; i < ctx.arr2.size(); ++i) {
    ctx.arr2[i] = ctx.src2 + i;  // [0xABE834+i*12] = [0xABBEC0]+i
  }

  // B. 内层完成数上限 [esp+0x40]
  //    fild dword [this+0x60] → fadd 0.32 → fmul 3.0 → _ftol
  const double limit_d = (static_cast<double>(ctx.this60) + 0.32) * 3.0;
  const std::int32_t limit = Ftol(limit_d);

  // C. 外层循环（[esp+0x24] 轮次 + [esp+0x18] 内层完成数）
  std::int32_t inner_done = 0;  // [esp+0x18]
  std::int32_t outer = 0;       // [esp+0x24]
  const std::int32_t side2 = ctx.side * ctx.side;

  while (inner_done < limit && outer < 10) {
    // 内层（0x5A50A3 起）
    std::int32_t success = 0;  // [esp+0x20] 成功点计数（上限 20）
    std::int32_t ebp = 0;      // 尝试计数（>200 强制 (0,0)）
    std::uint32_t coord = 0;   // [esp+0x14] 选中点坐标打包
    bool got = false;

    while (!got) {
      // 0x5A50BC：PickRange(0, side²-1)
      const std::int32_t v = PickRange(*ctx.rng, 0, side2 - 1);
      // work[v] 判空（0x50E470：work[v].xy==(0,0) → 重抽）
      if (ctx.world->WorkEmpty(static_cast<std::uint32_t>(v))) {
        continue;  // jne 0x5A50BC
      }
      ++ebp;  // 0x5A50F8 inc ebp
      // work[v] 坐标打包（0x5A50F9 mov eax,[ecx+edx]）
      coord = ctx.world->WorkCoord(static_cast<std::uint32_t>(v));
      if (ebp > 200) {
        // 0x5A5124：强制 (0,0)
        coord = 0;
        success = 0;  // [esp+0x28] 清零（golden 内 ebp 不超 200，仅防御照抄）
        got = true;
        break;
      }
      // GetCellAt → 0x486380 谓词（失败 → 重选，ebp 保留）
      const int x = static_cast<std::int16_t>(coord & 0xFFFF);
      const int y = static_cast<std::int16_t>(coord >> 16);
      if (!ctx.world->CellPass(x, y)) {
        continue;  // jmp 0x5A50B1
      }
      got = true;
    }

    // 0x5A513A：成功点计数 +1；[esp+0x14] = coord
    ++success;  // [esp+0x20]
    // 0x5A514E：计数 >= 20 → 跳过邻域检查直接概率判定
    if (success < 20) {
      const int x = static_cast<std::int16_t>(coord & 0xFFFF);
      const int y = static_cast<std::int16_t>(coord >> 16);
      const std::uint32_t idx = static_cast<std::uint32_t>(y * ctx.side + x);
      // 0x5A516F：work[idx].+0x45 != 0 → ±7 邻域扫描
      if (ctx.world->WorkFlag45(idx)) {
        bool hit = false;
        for (int nx = x - 7; nx <= x + 7 && !hit; ++nx) {
          for (int ny = y - 7; ny <= y + 7 && !hit; ++ny) {
            if (ctx.world->NeighborPass(nx, ny)) {
              hit = true;  // jne 0x5A522D → ebp=0 重抽
            }
          }
        }
        if (hit) {
          ebp = 0;
          got = false;
          continue;  // jmp 0x5A50B1（重新选点循环）
        }
      }
    }

    // 0x5A51C6 概率判定：r=Random() 无符号 → r·kInvUintMax
    const std::uint32_t r = ctx.rng->Random();
    const double scaled = static_cast<double>(r) * kInvUintMax;
    // 1.0-this60 vs scaled（fcompp：ST0>ST1 → 三连调；否则 magic）
    if (1.0 - static_cast<double>(ctx.this60) > scaled) {
      // 三连调路径（整数 this60 下不可达，照抄）
      if (ctx.world->Sub5A89D0()) {
        ctx.world->Sub5A82E0();
        ctx.world->Sub5A8720();
        ++inner_done;  // inc [esp+0x18]
      }
      // jmp 0x5A5348（不动 outer）
    } else {
      // magic 路径 0x5A5238
      if (success != 20) {  // [esp+0x20]==20 → 跳过五连调
        // magic 除法定标（0x5A5243-0x5A5273）
        //   mov ecx,H; mov eax,0x88888889; imul ecx,W → ecx=H*W
        //   shl ecx,1 → H*W*2; imul ecx → edx:eax = (int32)0x88888889*(H*W*2)
        //     ★0x88888889 作 int32 是负的（0x88888889>0x80000000）——imul
        //     单操作数按有符号乘，C++ 必须先截断 int32 再扩展
        //   add edx,ecx; sar edx,4; + (edx>>31) 符号修正 → magic
        const std::int32_t magic_in = static_cast<std::int32_t>(0x88888889);
        const std::int64_t mw = static_cast<std::int64_t>(ctx.map_h) *
                                static_cast<std::int64_t>(ctx.map_w) * 2;
        std::int64_t prod = static_cast<std::int64_t>(magic_in) * mw;  // imul
        std::int64_t edx = prod >> 32;              // imul 高位（有符号）
        edx += mw;                                   // add edx,ecx
        edx >>= 4;                                   // sar edx,4（算术）
        const std::int32_t sign =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(edx) >> 31);
        const std::int32_t magic =
            static_cast<std::int32_t>(edx) + sign;  // 符号修正
        // 双段选 scale/offset（0x5A5279-0x5A52CB）：
        //   段 1 预置：scale=300.0（[esp+0x48..4C]={0,0x4072C000}）、
        //              offset=magic（fild/fst 存回 double）
        //   0x5A5287 fcomp [2000]：magic-300 > 2000（C0=0&C3=0）→ je 段 2
        //   否则 0x5A529E fcomp [100]：magic+300 > 100 → je 段 1（用预置）
        //   落到段 2（0x5A52AB）：scale=[esp+0x48..4C]={0,0x408DB000}
        //   offset=[esp+0x30..34]={0,0x40906800}——完整 double 位模式
        //   0x408DB00000000000 = 950.0、0x4090680000000000 = 1050.0
        double scale = 300.0;    // 段 1 预置
        double offset = 0.0;
        const double m = static_cast<double>(magic);
        if (m - 300.0 > 2000.0) {
          // 段 2（magic-300 > 2000）
          std::uint64_t s2 = 0x408DB00000000000ULL;  // = 950.0
          std::uint64_t o2 = 0x4090680000000000ULL;  // = 1050.0
          std::memcpy(&scale, &s2, 8);
          std::memcpy(&offset, &o2, 8);
        } else {
          offset = m;  // 段 1 预置（magic 转 double）
          if (!(m + 300.0 > 100.0)) {
            // 段 2（magic-300<=2000 且 magic+300<=100）
            std::uint64_t s2 = 0x408DB00000000000ULL;
            std::uint64_t o2 = 0x4090680000000000ULL;
            std::memcpy(&scale, &s2, 8);
            std::memcpy(&offset, &o2, 8);
          }
        }
        // NormalDeviate 拒绝循环（0x5A52CF-0x5A52F9）
        double st = 0.0;
        do {
          st = ctx.world->NormalDeviate() * scale + offset;
        } while (st < 100.0 || st > 2000.0);  // 接受 [100,2000]
        const std::int32_t h = Ftol(st);      // _ftol（0x7C5F00）
        // 0x5A5308 call 0x5A5360（2 栈参 ret 8）——返回对象非 0 才继续
        if (ctx.world->Sub5A5360()) {
          ctx.world->Sub5A5B50();
          ctx.world->Sub5A6510();
          ctx.world->Sub5A6920();
          ctx.world->Sub5A78D0();
          // ★0x5A5333：mov ecx,[esp+0x18]; inc ecx; push 1;
          //   mov [esp+0x1c],ecx——push 后 [esp+0x1c]==push 前 [esp+0x18]！
          ++inner_done;  // "写错槽"实为内层完成数+1
          ctx.world->VfnCall();  // 0x5A5342 call [edx]（虚函数调用）
        }
      }
      ++outer;  // 0x5A5344 inc [esp+0x24]
    }
    // 0x5A5348：cmp [esp+0x24],0xA; jl 0x5A5091（外层循环头）
  }
}

}  // namespace rmg
}  // namespace ra2