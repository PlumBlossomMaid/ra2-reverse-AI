// 死代码复现 —— 0x5A6BA0 锚点邻域扫描器（110B，零取数点）
// （batch 1229）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A6BA0（110B）
// 调用约定：thiscall 无栈参 ret 0——ecx = 锚点 CellStruct
// {int16 x, int16 y}（movsx word 符号读）。
// 零引用死代码（TSV:1005 已 semantics 登记，batch 319；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 24 行实证，deadcode_5a6ba0_golden.txt）：
//   X = movsx word [ecx]；Y = movsx word [ecx+2]
//   loop：x ∈ [X-7, X+7) 外层（0x5A6BB1 lea edi,[ecx-7] 起，cmp edi,edx
//     / jge 退出返 1——X 边界先判）
//         y ∈ [Y-7, Y+7) 内层（0x5A6BB6 lea ebp,[eax-7] 起，cmp ebp,ebx
//     / jge 跳外层——Y 边界先判）
//         每格：{x,y} 写入栈上 CellStruct 结构 → GetCellAt(0x5657A0,
//           thiscall ecx=0x87F7E8, 1 栈参=坐标指针 ret 4) → cell 指针
//           → 0x4863D0(cell)：cell+0x38 落入多段阈值区间（真身长谓词，
//           本批打桩 mov eax,[ecx+0x38]; cmp eax,k; sete al; ret——
//           cell+0x38==k 才返 1）→ 谓词真 → 返 0（提前退出）
//   全邻域无命中 → 返 1
//   fail/成功都是 return al（0=邻域内有可用格 / 1=邻域全不可用）
//   ★全程不调 Randomizer——零取数点（golden next 字段实证：同种子
//     所有用例 next 相同 = 调用前后 RNG 状态不变）
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 零取数点（不消耗 RNG——与同族 58BE20 系的最大差异）；
//   ② ±7 邻域开区间 [X-7,X+7)×[Y-7,Y+7) 双层循环（外层 X 内层 Y）；
//   ③ GetCellAt → 0x4863D0 谓词链（邻域内有可用格 → 提前返 0）；
//   ④ 边界先判（X-7>=X+7 → 返 1；Y-7>=Y+7 → 跳外层——int16 锚点下
//      恒不触发，但原版存在该分支，语义照抄）。
#ifndef RA2_RMG_DEAD_CODE_5A6BA0_H_
#define RA2_RMG_DEAD_CODE_5A6BA0_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事：
class DeadCode5A6BA0World {
 public:
  virtual ~DeadCode5A6BA0World() {}

  // —— GetCellAt(0x5657A0) → 0x4863D0 谓词 ——
  // 原版：idx=y*512+x → cell 表项 → 0x4863D0 检查 cell+0x38 是否落入
  // 多段阈值区间（真身长谓词，本批 golden 打桩为 cell+0x38==k）。
  // C++ 侧 CellPass 返回「谓词通过」布尔。golden 按坐标装配 PASS/FAIL
  // cell 对象（+0x38==k → 通过）。
  virtual bool CellPass(int x, int y) = 0;
};

struct DeadCode5A6BA0Context {
  DeadCode5A6BA0World* world = nullptr;
  std::int16_t x = 0;  // 锚点（movsx word 符号读）
  std::int16_t y = 0;
};

// 0x5A6BA0。零引用死代码，语义按黄金逐条复刻。零取数点（不调 RNG）。
// 返回 0 = 邻域内有可用格（谓词真提前退出）；1 = 邻域全不可用。
int DeadCode5A6BA0(const DeadCode5A6BA0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A6BA0_H_