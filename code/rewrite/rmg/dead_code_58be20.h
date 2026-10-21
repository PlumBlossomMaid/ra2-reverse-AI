// 死代码复现 —— 0x58BE20 随机取「已占用」格子（batch 1225）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x58BE20（126B）
// 调用约定：thiscall 无栈参 ret 0，ecx = 输出指针（CellStruct {int16 x, int16 y}）。
// 零引用死代码（TSV:1008 已 semantics 登记，batch 320；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 40 行实证，deadcode_58be20_golden.txt）：
//   side  = [0x89C2DC]（MapGen 边长）
//   total = side*side                    （imul eax,eax）
//   scale = (double)(uint64)total        （fild qword [esp+0xC] 无符号读：
//     存 {eax=total, edi=0} 高 32 位显式 0）
//   loop: r = Random()（0xABE890 全局）→ mov {eax=r, edi=0} →
//         v = _ftol((double)(uint64)r * scale * [0x7ED898])
//                                       （fmul scale + fmul kInvUintMax，
//                                         RC=chop 链）
//         if (v > total-1) goto loop     （ja 无符号拒绝采样）
//         p = [0xABED10] + v*0x50        （work 数组每项 0x50B，
//           lea ecx,[eax+eax*4]; shl ecx,4 = v*0x50）
//         if (p->x == 0 && p->y == 0) goto loop   （空格子重抽）
//         *this = work[v] 首 dword       （x | (y<<16)）
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 拒绝采样双条件（v>total-1 无符号 ja + 空格子 (0,0) 均重抽，无限重抽）；
//   ② fild qword 无符号读（scale 与 r 都高 32 显式 0——non movzx）；
//   ③ side*0x50 索引（与活代码 0x578E60 分配 side²×0x50 同套）；
//   ④ 返回值 work[v] 首 dword = x|(y<<16)（不额外拼装）；
//   ⑤ RC=chop 全链（fmul 两次 + _ftol 截断在 FPCW=0x0E7F 下）。
#ifndef RA2_RMG_DEAD_CODE_58BE20_H_
#define RA2_RMG_DEAD_CODE_58BE20_H_

#include <cstdint>
#include <vector>

#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

struct DeadCode58BE20Context {
  // 全局 Randomizer [0xABE890]；拒绝采样每一步都消耗 1 个随机数。
  Randomizer* rng = nullptr;
  // [0x89C2DC] MapGen 边长（side）；total = side*side 项 work 数组。
  std::int32_t side = 0;
  // [0xABED10] work 数组：side*side 项，每项首 dword = x | (y<<16)
  // （低 16 = x、高 16 = y），即 golden 的 out 语义。
  std::vector<std::uint32_t> work;
};

// 0x58BE20。零引用死代码，语义按黄金逐条复刻。
// 返回值 = work[v] 首 dword（x | (y<<16)），成功才返回。
std::uint32_t DeadCode58BE20(const DeadCode58BE20Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_58BE20_H_