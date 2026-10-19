// 死代码复现 —— 0x5A76E0 菱形域矩形完整落点谓词（batch 932）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A76E0（0x5A76E0-0x5A78C3，483B）
// 调用约定：thiscall，ecx = this（16 字节结构），无栈参，普通 ret（0x5A78B9）。
// 零引用死代码（TSV:1005 已 semantics 登记，batch 319；2026-10-19 全段汇编
// 复核 + golden 10 用例实证，deadcode_76e0_golden.txt）。
//
// this 布局（golden 以 dword 灌，高 16 位恒 0）：
//   [this+0]  低 16 = x（0x5A76F3 movsx esi, cx，有符号扩展）
//   [this+4]  低 16 = y（0x5A76F6 movsx edx, di）
//   [this+8]  = w（int32，0x5A7730 mov edx, [ebx+8]）
//   [this+0xC] = h（int32，0x5A7778 mov edx, [ebx+0xC]）
//
// 语义链（0x5A76E9-0x5A78BA 逐段核对）：
//   · 四角菱形判定（0x5A76E9-0x5A77FA），任一失败 → 0x5A78BA 返回 0：
//       角 0 = (x, y)、角 1 = (x+w-1, y)、角 2 = (x, y+h-1)、
//       角 3 = (x+w-1, y+h-1)
//     每角四判据（对角 (X,Y)，判据顺序固定）：
//       X+Y <= W      → fail（0x5A7702 jle，W = [0xABED04]）
//       X-Y >= W      → fail（0x5A770E jge）
//       Y-X >= W      → fail（0x5A7718 jge）
//       X+Y >  W+2H   → fail（0x5A772A jg，W+2H = [0xABED08]）
//     == 菱形区域：W < X+Y <= W+2H 且 |X-Y| < W。
//     ★角坐标的截断：角 1/3 的 X = (int16)(w+raw_x-1)（0x5A773B movsx dx，
//       先 32 位 lea 再加，再截断回 16 位）；角 2/3 的 Y 同理
//       （0x5A7786 movsx dx）。角 0/2 的 X、角 0/1 的 Y = (int16)raw_x/y。
//   · 内块扫描（0x5A7800-0x5A7859，3×3）：外层 y ∈ {y..y+2}、内层 x ∈
//     {x..x+2}（x 优先，golden inner-fail gc=5 实锤遍历序）：
//       CellStruct{x16, y16} → GetCellAt(0x5657A0)（thiscall ecx=0x87F7E8
//       MapClass::Instance）→ tile = [cell+0x38]（dword）：
//         tile ∈ [PavedRoads, PavedRoads+1]（[0xABBEC8]，闭区间，内联
//         0x5A782C-0x5A7839 cmp/jl/inc/jle）→ 直接通过（不调谓词，
//         golden inner-paved+1 p730=0 实锤闭区间）
//         否则 0x486730（tile ∈ [Medians, +14)，[0xAA0E20]）→ 通过 or fail
//   · 边带扫描（0x5A785B-0x5A78AE，3 格 (x..x+2, y+3)）：
//       GetCellAt → tile，按序三谓词（任一通过继续下一格，全失败 → fail）：
//         0x486670（tile ∈ [PaveTile, +16)，[0xABC2B0]）→ 通过
//         0x486380（tile==0xFFFF || tile==0）→ 通过
//         0x486650（tile ∈ [MiscPaveTile, +14)，[0xAA10A4]）→ 通过
//   · 全过 → 0x5A78B3 mov al,1 返回 1。
//   · RNG 不参与（0x65C780 零调用）。
//
// 谓词反汇编（2026-10-19 dump）：
//   0x486670 / 0x486730 / 0x486650 三窗口谓词同构：
//     mov eax, [ecx+0x38]; mov ecx, [窗口]; cmp eax, ecx; jl 返 0;
//     add ecx, 跨度; cmp eax, ecx; jge 返 0; mov al,1 返 1
//     ——有符号 jl/jge，半开区间 [base, base+跨度)。跨度：670=0x10、
//     730/650=0x0E。
//   0x486380 两三点：cmp eax, 0xFFFF; je 返 1; test eax,eax; je 返 1; 返 0。
//
// 对拍点（golden 10 用例，deadcode_76e0_golden.txt）：返回 al + 五次
// 调用计数（gc：GetCellAt 次数、p670/p380/p650/p730：四谓词各调用次数）
// + GetCellAt 坐标序列。谓词与 GetCellAt 在 golden 中跑真身（hook 只
// 计数不劫持），C++ 侧 Fake 复刻同一语义（窗口比较）并计数。
//
// ★原版怪癖（照抄）：
//   ① 内块 paved 快路径是闭区间 [paved, paved+1]（cmp/jl/inc/jle），
//     而三个窗口谓词是半开区间 [base, base+size)——不同闭性，golden
//     inner-paved+1（tile=16∈[15,16] 直接过、p730=0）实证。
//   ② 角 1/3、2/3 的加宽坐标先 32 位求值再截断回 16 位（movsx dx）。
//   ③ 谓词/GetCellAt 全是有符号比较（jl/jge），tile 按有符号 int32 读。
#ifndef RA2_RMG_DEAD_CODE_76E0_H_
#define RA2_RMG_DEAD_CODE_76E0_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（被调族：GetCellAt 真身 + 四谓词真身，抽象成
// 世界接口以便对拍调用计数）：
class DeadCode76E0World {
 public:
  virtual ~DeadCode76E0World() {}

  // —— GetCellAt(0x5657A0) ——
  // ecx = 0x87F7E8（MapClass::Instance），栈参 = &CellStruct{x16, y16}。
  // 返回 [cell+0x38] 的 tile（dword，按有符号 int32 参与比较）。
  // 坐标以 CellStruct 低 16 位传递；C++ 侧直接收 int（用例范围无截断差）。
  virtual std::int32_t CellTile(int x, int y) = 0;

  // —— 0x486670（PaveTile 窗口）—— tile ∈ [窗口, 窗口+16)
  virtual bool IsPaveTile(std::int32_t tile) = 0;

  // —— 0x486380 —— tile == 0xFFFF || tile == 0
  virtual bool IsVoidTile(std::int32_t tile) = 0;

  // —— 0x486650（MiscPaveTile 窗口）—— tile ∈ [窗口, 窗口+14)
  virtual bool IsMiscPaveTile(std::int32_t tile) = 0;

  // —— 0x486730（Medians 窗口）—— tile ∈ [窗口, 窗口+14)
  virtual bool IsMedianTile(std::int32_t tile) = 0;
};

struct DeadCode76E0Context {
  DeadCode76E0World* world = nullptr;
  std::uint32_t raw_x = 0;    // [this+0]，只用低 16 位
  std::uint32_t raw_y = 0;    // [this+4]，只用低 16 位
  std::int32_t w = 0;         // [this+8]
  std::int32_t h = 0;         // [this+0xC]
  std::int32_t w_bound = 0;   // [0xABED04] = W（菱形 X+Y 下界）
  std::int32_t w2h_bound = 0; // [0xABED08] = W+2H（菱形 X+Y 上界）
  std::int32_t paved = 0;     // [0xABBEC8] PavedRoads（内联闭区间，见谓词 0x486730 前）
  std::int32_t medians = 0;   // [0xAA0E20] Medians 窗口（0x486730）
  std::int32_t pavet = 0;     // [0xABC2B0] PaveTile 窗口（0x486670）
  std::int32_t misc = 0;      // [0xAA10A4] MiscPaveTile 窗口（0x486650）
};

// 0x5A76E0。零引用死代码，语义按黄金逐条复刻。返回 al（1 = 全过，
// 0 = 任一判据失败）。
int DeadCode76E0(const DeadCode76E0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_76E0_H_