// 死代码复现 —— 0x58C320 随机取「已占用且类型匹配」格子（batch 1226）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x58C320（142B）
// 调用约定：thiscall **+1 栈参 ret 4**：ecx = this（比较基准，读 [this+8]
// dword）、[esp+4] = 输出指针（CellStruct {int16 x, int16 y}）。
// 零引用死代码（TSV:1009 已 semantics 登记，batch 320；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 45 行实证，deadcode_58c320_golden.txt）：
//   与 0x58BE20（batch 1225）同采样骨架，差异两处：
//   ① **多一道 `p->[0x38] == this->[8]` dword 比对**（0x58C382-0x58C388，
//      在空格判定之前）；不匹配回 0x58C335（scale 重算处，即重抽）。
//   ② thiscall+1 栈参（ecx=this、[esp+4]=输出指针，ret 4）；成功出口
//      `mov eax,[esp+0x20]`（pop 基址变换 = 原栈参1）写 work[v] 首 dword。
//   · side  = [0x89C2DC]；total = side*side（imul）
//   · loop: r = Random()（0xABE890 全局）→
//       v = _ftol((double)(uint64)r * (double)(uint64)total * kInvUintMax)
//         （fild qword ×2 + fmul scale + fmul [0x7ED898] + _ftol）
//       if ((unsigned)v > total-1) goto loop      （ja 拒绝采样）
//       p = [0xABED10] + v*0x50
//       if (p->[0x38] != this->[8]) goto loop      （★类型不匹配重抽）
//       if (p->x == 0 && p->y == 0) goto loop     （空格子重抽）
//       *out = work[v] 首 dword（x | (y<<16)）；ret 4
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 三重拒绝（v>total-1 + [0x38] 不匹配 + 空格 (0,0)，无限重抽）；
//   ② **[0x38] 判定先于空格判定**（0x58C382 vs 0x58C38A——次序照抄）；
//   ③ fild qword 无符号读（scale 与 r 都高 32 显式 0）；
//   ④ RC=chop 全链（FPCW=0x0E7F，MXCSR toward-zero 复刻）；
//   ⑤ work 每项 0x50B、v*0x50 索引（与活代码 0x578E60 分配同套）。
#ifndef RA2_RMG_DEAD_CODE_58C320_H_
#define RA2_RMG_DEAD_CODE_58C320_H_

#include <cstdint>
#include <vector>

#include "randomizer.h"

namespace ra2 {
namespace rmg {

// work 数组单条记录（0x50B 中 C++ 侧关心的两个字段）：
//   coord   = 首 dword，低 16 = x、高 16 = y（(0,0) 视为空格子）
//   field38 = +0x38 dword（类型/归属字段——判定对象）
struct DeadCode58C320Rec {
  std::uint32_t coord = 0;
  std::uint32_t field38 = 0;
};

struct DeadCode58C320Context {
  // 全局 Randomizer [0xABE890]；拒绝采样每一步都消耗 1 个随机数。
  Randomizer* rng = nullptr;
  // [0x89C2DC] MapGen 边长（side）；total = side*side 项 work 数组。
  std::int32_t side = 0;
  // this->[8] dword 比较基准（类型匹配判定的基准值）。
  std::uint32_t this8 = 0;
  // [0xABED10] work 数组：side*side 项，每项 {coord, field38}。
  std::vector<DeadCode58C320Rec> work;
};

// 0x58C320。零引用死代码，语义按黄金逐条复刻。
// 返回值 = work[v] 首 dword（x | (y<<16)），成功才返回。
std::uint32_t DeadCode58C320(const DeadCode58C320Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_58C320_H_