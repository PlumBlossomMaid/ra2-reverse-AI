// 死代码复现 —— 0x5A7440 菱形域矩形高度一致性校验（batch 935）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A7440（0x5A7440-0x5A763F，512B）
// 调用约定：thiscall 变体（ret 0，无栈参），ecx = 矩形结构指针、dl = flagA
// （第二寄存器参数，0x5A744E 存 [esp+0x13]）。零引用死代码
// （TSV:0x005A7440 未登记；2026-10-19 全段汇编复核 + golden 10 用例实证，
// deadcode_7440_golden.txt）。
//
// this 布局（golden 以 dword 灌，高 16 位恒 0）：
//   [this+0]  低 16 = x（0x5A7452 mov esi, [edi]；movsx edx, si）
//   [this+4]  低 16 = y（0x5A7454 mov ebx, [edi+4]；movsx ecx, bx）
//   [this+8]  = w（int32，0x5A7494 mov edx, [edi+8]）
//   [this+0xC] = h（int32，0x5A74DC mov edx, [edi+0xC]）
//
// 语义链（0x5A7440-0x5A763F 逐段核对，★与 0x5A7250 的差异）：
//   · 四角菱形判定（0x5A7449-0x5A755F，与 0x5A7250 完全相同）：
//       角 0 = (x, y)、角 1 = (x+w-1, y)、角 2 = (x, y+h-1)、
//       角 3 = (x+w-1, y+h-1)
//     每角四判据（对角 (X,Y)，判据顺序固定）：
//       X+Y <= W      → fail（W = [0xABED04]）
//       X-Y >= W      → fail
//       Y-X >= W      → fail
//       X+Y >  W+2H   → fail（W+2H = [0xABED08]）
//     == 菱形区域：W < X+Y <= W+2H 且 |X-Y| < W。
//     ★角坐标截断：角 1/3 的 X = (int16)(raw_x + w - 1)（0x5A7497
//       lea 32 位求值 → 0x5A749F movsx edx, cx 截断回 16）；角 2/3 的 Y
//       同理（0x5A74DF lea → 0x5A74EA movsx）。与 0x5A7250/0x5A76E0 同构。
//   ★· 起点格 GetCellAt((x,y))（0x5A7579，无条件在扫描前调用）→
//       读 [cell+0x11B]（movsx 有符号字节）→ 存 [esp+0x1C] 作为期望高度。
//   · 全矩形扫描（与 0x5A7250 同范围：整个 w×h 矩形，非 76E0 的 3×3+边带）：
//       外层 cy 从 raw_y 起、条件 cy < h+raw_y（0x5A758B add/0x5A7593
//       jge 0x5A762C：h<=0 → 直接成功返 1，但★起点格已查）；
//       内层 cx 从 raw_x 起、条件 cx < w+raw_x（0x5A759E add/0x5A75A2
//       jge 0x5A761B：w<=0 → 内层空，跳到外层递增）。
//       每格 (cx, cy) → GetCellAt(0x5657A0) → 高度一致性比对
//       （0x5A75C3 movsx edx,[cell+0x11B]; 0x5A75CA cmp edx,[esp+0x1C];
//       jne 0x5A7636 → 与起点格高度不等 → 整体 fail）→ 5 谓词链按序：
//         a. 0x4866D0（tile ∈ [paved, +0xf)，[0xABBEC8]）：过 →
//            flagA != 0 → 继续（0x5A760F）；flagA == 0 → 整体 fail
//            （0x5A760D je 0x5A7636）
//         b. 0x4866F0（tile ∈ [paved4, +4)，[0xABBEC4]）：过 →
//            flagA != 0 → 继续；== 0 → 整体 fail（同上，共用 dl）
//         c. 0x486380（tile==0xFFFF || tile==0）：过 → 直接继续
//            （0x5A75ED jne 0x5A760F，★不查 flag）
//         d. 0x486650（tile ∈ [misc, +0xE)，[0xAA10A4]）：过 → 直接继续
//         e. 0x486670（tile ∈ [pavet, +0x10)，[0xABC2B0]）：过 → 直接继续；
//            全失败 → 整体 fail（0x5A7603 je 0x5A7636）
//   · 全过 → 0x5A762F mov al,1 返回 1。RNG 不参与。
//
// ★与 0x5A7250（batch 934）的关键差异：
//   ① 7250 有 flagA(dl)+flagB(栈参) 双标志、ret 4；7440 只有 dl 单标志
//     （0x5A7607 mov al,[esp+0x13]，6D0/6F0 共用）、ret 0 无栈参。
//   ② 7440 多起点格期望高度 + 每格高度一致性比对（高度不等 → fail，
//     不查谓词）；7250 无高度概念。
//   ③ 谓词链命中路径相同：6D0/6F0 → flag 检查、380/650/670 → 直接继续。
//
// 谓词反汇编（2026-10-19 dump，与 batch 934 同源）：
//   0x4866D0：mov eax,[ecx+0x38]; mov ecx,[0xABBEC8]; cmp eax,ecx;
//     jl 返 0; add ecx,0xf; cmp eax,ecx; jge 返 0; mov al,1 返 1 ——
//     有符号半开区间 [paved, paved+0xf)（★宽度 0xf，batch 934 golden
//     实证：p670-pass tile=30 ∉ [15,30)）。
//   0x4866F0：同构 [paved4, paved4+4)，[0xABBEC4] 全局。
//   0x486380：cmp eax,0xFFFF; je 返 1; test eax,eax; je 返 1; 返 0。
//   0x486650：同构 [misc, misc+0xE)，[0xAA10A4]。
//   0x486670：同构 [pavet, pavet+0x10)，[0xABC2B0]。
//
// 对拍点（golden 10 用例，deadcode_7440_golden.txt）：返回 al + 六次
// 调用计数（gc：GetCellAt 次数、p6d0/p6f0/p380/p650/p670：五谓词各调用
// 次数）+ GetCellAt 坐标序列。谓词与 GetCellAt 在 golden 中跑真身
// （hook 只计数不劫持），C++ 侧 Fake 复刻同一语义（窗口比较）并计数。
//
// ★原版怪癖（照抄，golden 实证）：
//   ① 高度先于谓词：扫描格高度 != 起点格高度 → 立即 fail（不查谓词）。
//     示例 height-mismatch：gc=7（起点1+扫描6）但 p6d0=5（末格高度不等
//     未进谓词链）。
//   ② 双前置谓词门控共用 flagA：0x4866D0/0x4866F0 通过后检查 flagA，
//     flag 为 0 时整体失败——golden flagA-zero gc=2 实证首格即 fail
//     （起点格 + 首扫描格）。
//   ③ 起点格无条件先查：h-zero/w-zero gc=1（四角过、矩形空，但起点格
//     GetCellAt 照查）。
//   ④ 谓词链序：6D0 → 6F0 → 380 → 650 → 670，前两个带 flag 门控，
//     后三个直接继续。与 7250 相同；与 76E0 的边带序（670→380→650）不同。
//   ⑤ 角 1/3、2/3 的加宽坐标先 32 位求值再截断回 16 位（movsx）。
//   ⑥ 谓词/GetCellAt 全是有符号比较（jl/jge），tile 按有符号 int32 读；
//     cell+0x11B 高度按有符号字节（movsx）读。
#ifndef RA2_RMG_DEAD_CODE_7440_H_
#define RA2_RMG_DEAD_CODE_7440_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（被调族：GetCellAt 真身 + 五谓词真身，抽象成
// 世界接口以便对拍调用计数）：
class DeadCode7440World {
 public:
  virtual ~DeadCode7440World() {}

  // —— GetCellAt(0x5657A0) ——
  // ecx = 0x87F7E8（MapClass::Instance），栈参 = &CellStruct{x16, y16}。
  // 返回 [cell+0x38] 的 tile（dword，按有符号 int32 参与比较）。
  virtual std::int32_t CellTile(int x, int y) = 0;

  // —— CellClass+0x11B 高度字节（movsx 有符号读）——
  // 0x5A7584 movsx ecx, byte ptr [eax+0x11B]；比较用签名 int8。
  virtual std::int8_t CellHeight(int x, int y) = 0;

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

struct DeadCode7440Context {
  DeadCode7440World* world = nullptr;
  std::uint32_t raw_x = 0;    // [this+0]，只用低 16 位
  std::uint32_t raw_y = 0;    // [this+4]，只用低 16 位
  std::int32_t w = 0;         // [this+8]
  std::int32_t h = 0;         // [this+0xC]
  std::uint8_t flag_a = 0;    // dl（0x5A744E 存 [esp+0x13]）
  std::int32_t w_bound = 0;   // [0xABED04] = W（菱形 X+Y 下界）
  std::int32_t w2h_bound = 0; // [0xABED08] = W+2H（菱形 X+Y 上界）
  std::int32_t paved = 0;     // [0xABBEC8]（0x4866D0 窗口基）
  std::int32_t paved4 = 0;    // [0xABBEC4]（0x4866F0 窗口基）
  std::int32_t pavet = 0;     // [0xABC2B0] PaveTile（0x486670）
  std::int32_t misc = 0;      // [0xAA10A4] MiscPaveTile（0x486650）
};

// 0x5A7440。零引用死代码，语义按黄金逐条复刻。返回 al（1 = 全过，
// 0 = 任一判据/高度比对/谓词链失败）。
int DeadCode7440(const DeadCode7440Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_7440_H_