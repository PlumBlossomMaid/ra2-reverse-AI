// 死代码复现 —— 0x58C4A0 随机取「已占用且类型匹配且谓词通过」格子
// （batch 1227）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x58C4A0（231B）
// 调用约定：thiscall **+1 栈参 ret 4**：ecx = this（读 [this+8] dword
// 比较基准）、[esp+4] = 输出指针（CellStruct {int16 x, int16 y}）。
// 零引用死代码（TSV:1010 已 semantics 登记，batch 320；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 55 行实证，deadcode_58c4a0_golden.txt）：
//   side  = [0x89C2DC]；total = side*side（imul）
//   scale = (double)(uint64)total       （fild qword 无符号读）
//   loop（0x58C4C7 起循环头）：
//     r = Random()（0xABE890 全局）
//     v = _ftol((double)(uint64)r * scale * kInvUintMax)   （RC=chop 链）
//     if ((unsigned)v > total-1) goto loop                 （ja 拒绝采样）
//     ★rep movsd 把 work[v] 的 0x14 dword（80B）复制到 work[0]
//       （edi=ebp=[0xABED10] work 基址）——「当前格」= 复制后 work[0]
//     ★ebx 尝试计数++（已在 0x58C518 inc）
//     if (ebx >= 100) goto fail（0x58C567）   ← 100 次上限
//     if (work[0].[0x38] != this->[8]) goto loop    （类型不匹配重抽）
//     if (work[0].x == 0 && work[0].y == 0) goto loop（空格子重抽）
//     if (work[0].[0x44] byte == 0) goto loop        （★field44 判定）
//     ★cell = GetCellAt(0x5657A0, &work[0])（thiscall ecx=0x87F7E8）
//     if (0x486380(cell) == 0) goto loop             （★谓词判定）
//     *out = work[0] 首 dword（x | (y<<16)）；ret 4
//   fail（0x58C567）：*out = (0,0)；ret 4（不额外采样——已消耗的
//     Random 不退，golden next 校验第 100 次采样之后的值）
//
// 与原版 GetCellAt/0x486380 对应（golden 探针装配 CELL_OBJ）：
//   0x5657A0：idx = y*512+x（movsx word）；越界/NULL → 哑元 0xABDC50
//   0x486380：cell+0x38 == 0xFFFF || == 0 → al=1（通过）否则 0
//   C++ 侧把「coor→cell→谓词」抽成一个 CellPass(x,y) 世界回调。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① **100 次尝试上限**（cmp ebx,0x64 / jge 失败）——失败写 (0,0)
//      且「已花掉的随机数不退回」；
//   ② **rep movsd 复制到 work[0] 工作区**——判定与输出都读复制后的
//      work[0]（不是直接读 work[v]）；
//   ③ **三重前置判定 + 谓词**：类型不匹配 → 空格 → field44==0 →
//      GetCellAt 谓词（顺序照抄）；
//   ④ fild qword 无符号读 + RC=chop 全链（FPCW=0x0E7F）；
//   ⑤ 空格判定读 word x / word y 两字段（(0,0) 失败）；
//   ⑥ 返回 work[0] 首 dword（= work[v] 复制后的首项）；
//   ⑦ 每轮只消耗 1 个随机数，重抽/拒绝都消耗（同族怪癖表）。
#ifndef RA2_RMG_DEAD_CODE_58C4A0_H_
#define RA2_RMG_DEAD_CODE_58C4A0_H_

#include <cstdint>
#include <vector>

#include "randomizer.h"

namespace ra2 {
namespace rmg {

// work 数组单条记录（0x50B 中 C++ 侧关心的三个字段）：
//   coord    = 首 dword，低 16 = x、高 16 = y（(0,0) 视为空格子）
//   field38  = +0x38 dword（类型/归属字段——与 this->[8] 比较）
//   field44  = +0x44 byte（第三重前置判定：非 0 才继续）
struct DeadCode58C4A0Rec {
  std::uint32_t coord = 0;
  std::uint32_t field38 = 0;
  std::uint8_t field44 = 0;
};

// 这一阶段要问外界的事：
class DeadCode58C4A0World {
 public:
  virtual ~DeadCode58C4A0World() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（拒绝采样，每轮消耗 1 个）
  virtual Randomizer& Rng() = 0;

  // —— GetCellAt(0x5657A0) → 0x486380 谓词 ——
  // 原版：idx=y*512+x → cell 表项 → cell+0x38==0xFFFF||==0 通过（真）；
  // 越界/表项 NULL → 哑元 0xABDC50 回落（哑元 +0x38 视实现内容）。
  // C++ 侧 CellPass 返回「谓词通过」布尔。golden 按 CELL_OBJ.tile
  // 统一装配（0 → 通过、非 0 非 0xFFFF → 拦截）。
  virtual bool CellPass(int x, int y) = 0;
};

struct DeadCode58C4A0Context {
  Randomizer* rng = nullptr;
  DeadCode58C4A0World* world = nullptr;
  std::int32_t side = 0;   // [0x89C2DC]
  std::uint32_t this8 = 0; // [this+8] 类型比较基准
  // [0xABED10] work 数组：side*side 项 {coord, field38, field44}。
  std::vector<DeadCode58C4A0Rec> work;
};

// 0x58C4A0。零引用死代码，语义按黄金逐条复刻。
// 成功：返回 work[v] 首 dword（x | (y<<16)）。
// 100 次上限后：返回 0（失败出口 (0,0)——不额外采样）。
std::uint32_t DeadCode58C4A0(const DeadCode58C4A0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_58C4A0_H_