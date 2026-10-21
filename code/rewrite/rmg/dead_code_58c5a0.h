// 死代码复现 —— 0x58C5A0 随机取「已占用且类型匹配且双谓词通过」格子
// （batch 1228）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x58C5A0（259B）
// 调用约定：ecx = 输出指针（[esp+0x14] 槽，出入口都读它）、
//   edx = arg2（[esp+0x1c] 存，0x56D230 返回值比较基准）、
//   栈参1 = arg3（[esp+0x3c]，==-1 跳过 [0x38] 比对，否则须相等）；ret 4
//   （清 1 个栈参 = arg3；ecx 无栈参、edx 无栈参）
// 零引用死代码（「四级递增严格度」末级；batch 320 同批登记，
// 2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 40 行实证，deadcode_58c5a0_golden.txt）：
//   side  = [0x89C2DC]；total = side*side（imul）
//   scale = (double)(uint64)total       （fild qword 无符号读）
//   loop（0x58C5E3 起循环头）：
//     r = Random()（0xABE890 全局）
//     v = _ftol((double)(uint64)r * scale * kInvUintMax)   （RC=chop 链）
//     if ((unsigned)v > total-1) goto loop                 （ja 拒绝采样）
//     ★rep movsd 把 work[v] 的 0x14 dword（80B）复制到 work[0]
//       （edi=ebx=[0xABED10] work 基址）——「当前格」= 复制后 work[0]
//     ★尝试计数 [esp+0x10]：inc → cmp 0x64 → rep movsd → 存回 → jge fail
//       （与 0x58C4A0 相同的 inc-before-cmp：第 100 次被接受的采样即失败）
//     ★cell = GetCellAt(0x5657A0, &work[0])（thiscall ecx=0x87F7E8，
//       1 栈参=坐标指针 ret 4）→ esi = cell
//     if (work[0].x == 0 && work[0].y == 0) goto loop     （空格子重抽）
//     ★if (0x56D230(0x87F7E8, cell+0x24, 0, 0) != arg2) goto loop
//       （thiscall +3 栈参 ret 0xC；golden 打桩返回固定值；真身语义
//       留后续批次——反汇编已读大半：返回值查 [edi+eax*4+0x18] 表）
//     if (0x486380(cell) == 0) goto loop                  （★谓词判定：
//       cell+0x38 == 0xFFFF || == 0 → al=1 通过）
//     if (arg3 != -1 && work[0].[0x38] != arg3) goto loop （arg3==-1 跳过）
//     *out = work[0] 首 dword（x | (y<<16)）；ret 4
//   fail（0x58C685）：*out = (0,0)；ret 4（不额外采样——已消耗的
//     Random 不退，golden next 校验第 100 次采样之后的值）
//
// ★与 0x58C4A0 的差异（「四级递增严格度」末级）：
//   ① 调用约定多 edx=arg2（0x56D230 返回值比较基准）与栈参 arg3
//      （==-1 跳过 [0x38] 比对）；
//   ② 第三重前置判定换成 0x56D230 谓词（0x58C4A0 是 field44==0）——
//      **本函数无 field44 检查**；
//   ③ 判定顺序：GetCellAt → 空格 → 0x56D230 → 0x486380 → arg3
//      （0x58C4A0：类型 → 空格 → field44 → GetCellAt 谓词）。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 100 次尝试上限（同 0x58C4A0——inc-before-cmp，第 100 次失败，
//      随机数不回退）；
//   ② rep movsd 复制 work[v] 80B → work[0] 工作区（判定/输出都读
//      复制后的 work[0]）；
//   ③ GetCellAt 先于空格判定（0x58C637 call 在 0x58C63C cmp 之前）；
//   ④ arg3 == -1 时跳过 [0x38] 比对（成功出口 0x58C6A3 直达）；
//   ⑤ fild qword 无符号读 + RC=chop 全链（FPCW=0x0E7F）；
//   ⑥ 每轮只消耗 1 个随机数，重抽/拒绝/上限失败都消耗（同族怪癖表）。
#ifndef RA2_RMG_DEAD_CODE_58C5A0_H_
#define RA2_RMG_DEAD_CODE_58C5A0_H_

#include <cstdint>
#include <vector>

#include "randomizer.h"

namespace ra2 {
namespace rmg {

// work 数组单条记录（0x50B 中 C++ 侧关心的两个字段）：
//   coord    = 首 dword，低 16 = x、高 16 = y（(0,0) 视为空格子）
//   field38  = +0x38 dword（类型/归属字段——arg3 != -1 时须 == arg3）
struct DeadCode58C5A0Rec {
  std::uint32_t coord = 0;
  std::uint32_t field38 = 0;
};

// 这一阶段要问外界的事：
class DeadCode58C5A0World {
 public:
  virtual ~DeadCode58C5A0World() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（拒绝采样，每轮消耗 1 个）
  virtual Randomizer& Rng() = 0;

  // —— 0x56D230(0x87F7E8, cell+0x24, 0, 0) ——
  // 返回值须 == arg2 才通过判定（thiscall+3 栈参 ret 0xC）。golden 探针
  // 在 0x56D230 入口打桩（mov eax,imm; ret 0xC）注入固定返回值；真身
  // 语义（查 [edi+eax*4+0x18] 表）留后续批次。C++ 侧 D56Value(x,y)
  // 返回「该格子的 0x56D230 值」。
  virtual std::uint32_t D56Value(int x, int y) = 0;

  // —— GetCellAt(0x5657A0) → 0x486380 谓词 ——
  // 原版：idx=y*512+x → cell 表项 → cell+0x38==0xFFFF||==0 通过（真）；
  // 越界/表项 NULL → 哑元 0xABDC50 回落（哑元 +0x38 视实现内容）。
  // C++ 侧 CellPass 返回「谓词通过」布尔。golden 按 CELL_OBJ.tile
  // 统一装配（0 → 通过、0x40 → 拦截）。
  virtual bool CellPass(int x, int y) = 0;
};

struct DeadCode58C5A0Context {
  Randomizer* rng = nullptr;
  DeadCode58C5A0World* world = nullptr;
  std::int32_t side = 0;    // [0x89C2DC]
  std::uint32_t arg2 = 0;   // edx：0x56D230 返回值比较基准
  std::int32_t arg3 = -1;   // 栈参1：==-1 跳过 [0x38] 比对
  // [0xABED10] work 数组：side*side 项 {coord, field38}。
  std::vector<DeadCode58C5A0Rec> work;
};

// 0x58C5A0。零引用死代码，语义按黄金逐条复刻。
// 成功：返回 work[v] 首 dword（x | (y<<16)）。
// 100 次上限后：返回 0（失败出口 (0,0)——不额外采样）。
std::uint32_t DeadCode58C5A0(const DeadCode58C5A0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_58C5A0_H_