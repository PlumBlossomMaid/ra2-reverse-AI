// 死代码复现 —— 0x5A7250 菱形域矩形扫描校验（batch 934）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A7250（0x5A7250-0x5A7435，486B）
// 调用约定：thiscall 变体 + 1 栈参（ret 4），ecx = 矩形结构指针、dl = flagA
// （第二寄存器参数，存 [esp+0xb]）、栈参 1 = flagB（低字节，0x5A73D4 读
// [esp+0x24]）。零引用死代码（TSV:721 已 semantics 登记，batch 222；
// 2026-10-19 全段汇编复核 + golden 14 用例实证，deadcode_7250_golden.txt）。
//
// this 布局（golden 以 dword 灌，高 16 位恒 0）：
//   [this+0]  低 16 = x（0x5A7268 movsx esi, ax）
//   [this+4]  低 16 = y（0x5A726B movsx edx, bx）
//   [this+8]  = w（int32，0x5A72AA mov edx, [ebp+8]）
//   [this+0xC] = h（int32，0x5A72EE mov esi, [ebp+0xC]）
//
// 语义链（0x5A7250-0x5A7433 逐段核对）：
//   · 四角菱形判定（0x5A7262-0x5A7377），任一失败 → 0x5A742A 返回 0：
//       角 0 = (x, y)、角 1 = (x+w-1, y)、角 2 = (x, y+h-1)、
//       角 3 = (x+w-1, y+h-1)
//     每角四判据（对角 (X,Y)，判据顺序固定）：
//       X+Y <= W      → fail（0x5A727C jle，W = [0xABED04]）
//       X-Y >= W      → fail（0x5A7288 jge）
//       Y-X >= W      → fail（0x5A7292 jge）
//       X+Y >  W+2H   → fail（0x5A72A4 jg，W+2H = [0xABED08]）
//     == 菱形区域：W < X+Y <= W+2H 且 |X-Y| < W。
//     ★角坐标截断：角 1/3 的 X = (int16)(raw_x + w - 1)（0x5A72AD
//       lea 32 位求值 → 0x5A72B1 movsx edi, si 截断回 16）；角 2/3 的 Y
//       同理（0x5A72F5 lea → 0x5A7300 movsx）。角 0/2 的 X、角 0/1 的 Y
//       = (int16)raw_x/y。与 0x5A76E0（batch 932）同构。
//   · 全矩形扫描（★对比 76E0 的 3×3+边带，这里是整个 w×h 矩形）：
//       外层 cy 从 raw_y 起、条件 cy < h+raw_y（0x5A7386 add/0x5A738A
//       jge 0x5A741E：h<=0 → 直接成功返 1，跳过扫描）；
//       内层 cx 从 raw_x 起、条件 cx < w+raw_x（0x5A7392 lea/0x5A7395
//       jge 0x5A740D：w<=0 → 内层空，跳到外层递增）。
//       每格 (X=di 低16, Y=bx 低16) → GetCellAt(0x5657A0)（thiscall
//       ecx=0x87F7E8 MapClass::Instance，栈参=&CellStruct{x16,y16}）
//       → tile = [cell+0x38]（dword，有符号参与比较）→ 5 谓词链按序：
//         a. 0x4866D0（tile ∈ [paved, +0xf)，[0xABBEC8]）：过 →
//            flagA != 0 → 继续（0x5A73C7 jmp）；flagA == 0 → 整体 fail
//            （0x5A73C5 je 0x5A742A）
//         b. 0x4866F0（tile ∈ [paved4, +4)，[0xABBEC4]）：过 →
//            flagB != 0 → 继续；== 0 → 整体 fail（0x5A73DA je）
//         c. 0x486380（tile==0xFFFF || tile==0）：过 → 继续
//            （0x5A73E7 jne 0x5A73FF）
//         d. 0x486650（tile ∈ [misc, +0xE)，[0xAA10A4]）：过 → 继续
//         e. 0x486670（tile ∈ [pavet, +0x10)，[0xABC2B0]）：过 → 继续；
//            全失败 → 整体 fail（0x5A73FD je 0x5A742A）
//   · 全过 → 0x5A7421 mov al,1 返回 1。RNG 不参与。
//
// 谓词反汇编（2026-10-19 dump）：
//   0x4866D0：mov eax,[ecx+0x38]; mov ecx,[0xABBEC8]; cmp eax,ecx;
//     jl 返 0; add ecx,0xf; cmp eax,ecx; jge 返 0; mov al,1 返 1
//     ——有符号半开区间 [paved, paved+0x10)。
//   0x4866F0：mov ecx,[0xABBEC4]; jl 返 0; add ecx,4; cmp jge 返 0
//     ——半开区间 [paved4, paved4+4)。[0xABBEC4] 新全局（batch 934
//     首次登记，PavedRoads-4）。
//   0x486380：cmp eax,0xFFFF; je 返 1; test eax,eax; je 返 1; 返 0。
//   0x486650：同构 [misc, misc+0xE)；0x486670：同构 [pavet, pavet+0x10)。
//
// 对拍点（golden 14 用例，deadcode_7250_golden.txt）：返回 al + 六次
// 调用计数（gc：GetCellAt 次数、p6d0/p6f0/p380/p650/p670：五谓词各调用
// 次数）+ GetCellAt 坐标序列。谓词与 GetCellAt 在 golden 中跑真身
// （hook 只计数不劫持），C++ 侧 Fake 复刻同一语义（窗口比较）并计数。
//
// ★原版怪癖（照抄）：
//   ① 双前置谓词门控：0x4866D0/0x4866F0 通过后还要检查 flagA/flagB，
//     flag 为 0 时**整体失败**（不是跳过该格）——golden flagA-zero /
//     flagB-zero gc=1 实证首格即 fail。
//   ② 扫描范围是整矩形：外层 h<=0 直接成功返 1（h-zero gc=0）、内层
//     w<=0 空循环（w-zero gc=0 但四角仍判）。
//   ③ 谓词链与 76E0 的边带序（670→380→650）不同：7250 是
//     6D0 → 6F0 → 380 → 650 → 670，且前两个带 flag 门控。
//   ④ 角 1/3、2/3 的加宽坐标先 32 位求值再截断回 16 位（movsx）。
//   ⑤ 谓词/GetCellAt 全是有符号比较（jl/jge），tile 按有符号 int32 读。
#ifndef RA2_RMG_DEAD_CODE_7250_H_
#define RA2_RMG_DEAD_CODE_7250_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（被调族：GetCellAt 真身 + 五谓词真身，抽象成
// 世界接口以便对拍调用计数）：
class DeadCode7250World {
 public:
  virtual ~DeadCode7250World() {}

  // —— GetCellAt(0x5657A0) ——
  // ecx = 0x87F7E8（MapClass::Instance），栈参 = &CellStruct{x16, y16}。
  // 返回 [cell+0x38] 的 tile（dword，按有符号 int32 参与比较）。
  virtual std::int32_t CellTile(int x, int y) = 0;

  // —— 0x4866D0 —— tile ∈ [paved, paved+0xf)（[0xABBEC8]，★宽度 0xf）
  virtual bool IsPaved0(std::int32_t tile) = 0;

  // —— 0x4866F0 —— tile ∈ [paved4, paved4+4)（[0xABBEC4]）
  virtual bool IsPaved4(std::int32_t tile) = 0;

  // —— 0x486380 —— tile == 0xFFFF || tile == 0
  virtual bool IsVoidTile(std::int32_t tile) = 0;

  // —— 0x486650 —— tile ∈ [misc, misc+0xE)（[0xAA10A4]）
  virtual bool IsMiscPaveTile(std::int32_t tile) = 0;

  // —— 0x486670 —— tile ∈ [pavet, pavet+0x10)（[0xABC2B0]）
  virtual bool IsPaveTile(std::int32_t tile) = 0;
};

struct DeadCode7250Context {
  DeadCode7250World* world = nullptr;
  std::uint32_t raw_x = 0;    // [this+0]，只用低 16 位
  std::uint32_t raw_y = 0;    // [this+4]，只用低 16 位
  std::int32_t w = 0;         // [this+8]
  std::int32_t h = 0;         // [this+0xC]
  std::uint8_t flag_a = 0;    // dl（0x5A7257 存 [esp+0xb]）
  std::uint8_t flag_b = 0;    // 栈参 1 低字节（0x5A73D4 读 [esp+0x24]）
  std::int32_t w_bound = 0;   // [0xABED04] = W（菱形 X+Y 下界）
  std::int32_t w2h_bound = 0; // [0xABED08] = W+2H（菱形 X+Y 上界）
  std::int32_t paved = 0;     // [0xABBEC8]（0x4866D0 窗口基）
  std::int32_t paved4 = 0;    // [0xABBEC4]（0x4866F0 窗口基）
  std::int32_t pavet = 0;     // [0xABC2B0] PaveTile（0x486670）
  std::int32_t misc = 0;      // [0xAA10A4] MiscPaveTile（0x486650）
};

// 0x5A7250。零引用死代码，语义按黄金逐条复刻。返回 al（1 = 全过，
// 0 = 任一判据/谓词链失败）。
int DeadCode7250(const DeadCode7250Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_7250_H_