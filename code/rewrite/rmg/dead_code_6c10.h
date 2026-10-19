// 死代码复现 —— 0x5A6C10 [0xA8ED2C] 表查询放置（batch 933）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A6C10（0x5A6C10-0x5A6D73）
// 调用约定：thiscall + 2 栈参（ret 8）：
//   ecx = 表索引、edx = &CellStruct{int16 X, int16 Y}（源坐标，只读）、
//   栈参1 = work 写入值、栈参2 = 层高调整量（== -1 跳过 cell+0x11B 写）。
// 零引用死代码（TSV:470 已 semantics 登记，batch 138；2026-10-19 全段
// 汇编复核 + golden 10 用例实证，deadcode_6c10_golden.txt）。
//
// 语义链（0x5A6C10-0x5A6D73 逐段核对）：
//   · 表项 = [0xA8ED2C] 全局表基址[ecx*4]（4B/项 dword 指针数组）。
//   · 表项虚表槽 0x9C（call [edx+0x9C]，thiscall ecx=表项）→ 数据表基址；
//     返回 0 → 0x5A6C32 je 直接返回。数据表：+0x10 起 4B/项指针数组
//     （golden 打桩返回各表项自己的数据表基址）。
//   · 表项 +0x2E8 = 外层计数、+0x2E4 = 内层计数（任一 <=0 → 返回，
//     0x5A6C46/0x5A6C5E jle）。
//   · 双层循环（0x5A6C54 内层 / 0x5A6D59 外层）：内层索引 edi 加到 X、
//     外层索引 ebx 加到 Y（16 位加法，mov ax,[edx] + add ax,di /
//     mov cx,bx + add cx,[edx+2]——x 优先遍历序）：
//         X = (int16)(源X + edi)，Y = (int16)(源Y + ebx)
//       菱形四判定（[0xABED04]=W、[0xABED08]=W+2H，有符号）：
//         X+Y <= W → 跳过（0x5A6C92 jle）
//         X-Y >= W → 跳过（0x5A6C9E jge）
//         Y-X >= W → 跳过（0x5A6CA8 jge）
//         X+Y >  W+2H → 跳过（0x5A6CB4 jg）
//       → GetCellAt(0x5657A0)（thiscall ecx=0x87F7E8 MapClass::Instance）
//         → cell 指针。
//       → 索引串 = 外层索引 × 内层计数 + 内层索引（imul ecx,ebx 后
//         add ecx,edi——ebx 是外层索引，0x5A6CD7）；数据项 =
//         数据表[索引串*4 + 0x10]。
//       → 数据项非 0 时写入（0x5A6CE2 je 跳过写 cell 与 work，但
//         GetCellAt 照调——golden data-zero gc=6/CELL=5 实锤）：
//           cell+0x11A = 索引串低 8 位（cl）
//           cell+0x38  = 表项+0x294（dword）
//           若 栈参2 != -1：cell+0x11B = 数据项+0x28 + 栈参2 - 4
//               （0x5A6CF7 cmp -1 / je，golden layer-m1 c11b=0 实锤）
//           cell+0x11C = 数据项+0x2A
//           若 [0xABED10]（work）非 0：work[(Y*side+X)*0x50 + 0x38] =
//               栈参1（movsx Y → imul side → add X → lea *5 → shl 4 =
//               *80=WORK_SIZE，0x5A6D1D-0x5A6D3A）
//   · 返回 EAX 无意义（0x5A6D6C 弹栈后 ret 8，eax 残渣），C++ 返回 0
//     对拍（golden CASE 行无 ret 字段）。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 菱形四判定含 X-Y>=W / Y-X>=W 两个交叉判据（非仅 |X+Y| 范围）；
//     partial-fail 用例 gc=21 实证：X>=18(j=0)/X>=19(j=1) 即被 X-Y 判据
//     拦截（注释预估 28 有误，golden 机器码真值 21）。
//   ② 索引串 = 外层×内层 + 内层（非 内层×外层 + 内层——0x5A6CD7 先
//     imul ecx,ebx 时 ecx=内层计数、ebx=外层索引）；c11a 序列
//     0..5（all-pass）/16..26（partial-fail j=1 行）实锤。
//   ③ 数据项非 0 才写 cell 与 work；数据项 0 时 GetCellAt 照调、写全
//     跳过（0x5A6CE2 je 到 0x5A6D3E 内层尾，连 work 一起跳过）。
//   ④ 层高调整 -1 只跳过 cell+0x11B；cell+0x11A/+0x38/+0x11C 照写。
//   ⑤ 每个表项独立虚表槽 0x9C（golden 逐表项打桩返回各自数据表——
//     index-1 用例查第二表项取自己的数据表与 c294=0x5678 实锤）。
//
// 对拍点（golden 10 用例）：GetCellAt 次数 + 坐标序列 + CELL 字段写入
// {c38, c11a, c11b, c11c} + WORK 写入 {坐标, 值}。
#ifndef RA2_RMG_DEAD_CODE_6C10_H_
#define RA2_RMG_DEAD_CODE_6C10_H_

#include <cstdint>
#include <vector>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（被调族：GetCellAt 真身 + 表项/数据表/虚表槽
// 0x9C 环境，抽象成世界接口以便对拍调用计数与写入序列）：
class DeadCode6C10World {
 public:
  virtual ~DeadCode6C10World() {}

  // —— GetCellAt(0x5657A0) ——
  // ecx = 0x87F7E8（MapClass::Instance），栈参 = &CellStruct{x16, y16}。
  // 返回 cell 句柄（后续写 cell+0x38/+0x11A/+0x11B/+0x11C）。
  // 实现须在调用处记录坐标序列（对拍 golden GC 行）。
  virtual void* GetCellAt(int x, int y) = 0;

  // —— 写 cell 字段 ——
  // golden 侧真身直接写 [cell+0x38]/[+0x11A]/[+0x11B]/[+0x11C]；C++ 侧
  // 实现记录 CELL 行 {c38, c11a, c11b, c11c}。c11b 是否写由调用方决定
  // （层高调整 -1 时传 0 且不写——对拍 c11b=0 等价）。
  virtual void WriteCell(void* cell, std::uint32_t c38, std::uint8_t c11a,
                         std::uint8_t c11b, std::uint8_t c11c) = 0;

  // —— work 写入 ——
  // [0xABED10] 非 0 才写 work[(Y*side+X)*0x50 + 0x38] = workv。传入坐标
  // 与值，索引计算由调用方（主函数）完成——golden 记录 WORK 行 {x, y, v}。
  virtual void WriteWork(int x, int y, std::int32_t workv) = 0;

  // —— [0xABED10] 是否可用 ——
  // golden work-zero 用例 [0xABED10]=0 → work 写入整个跳过（但 cell
  // 写入照常）。C++ 侧实现据此分支。
  virtual bool WorkPresent() = 0;
};

// 表项（[0xA8ED2C][index] 指向的对象）：
struct DeadCode6C10DataItem {
  bool present = false;     // 原版数据项 0 = 跳过写 cell/work（GetCellAt 照调）
  std::uint8_t b28 = 0;     // 数据项 +0x28（cell+0x11B = b28 + layer - 4）
  std::uint8_t b2a = 0;     // 数据项 +0x2A（cell+0x11C）
};

struct DeadCode6C10TableItem {
  std::int32_t inner_count = 0;      // 表项 +0x2E4
  std::int32_t outer_count = 0;      // 表项 +0x2E8
  std::uint32_t c294 = 0;            // 表项 +0x294（cell+0x38 写入值）
  bool slot9c_ok = false;            // 槽 0x9C 是否存在（false → 返回 0）
  // 数据表：索引串（外层×内层计数 + 内层）→ 数据项
  std::vector<DeadCode6C10DataItem> data;
};

struct DeadCode6C10Context {
  DeadCode6C10World* world = nullptr;
  // [0xA8ED2C] 全局表（golden 用例按 index 查表，表项指针按 index 取）
  const std::vector<DeadCode6C10TableItem>* table = nullptr;
  std::int32_t index = 0;            // ecx 表索引
  std::int16_t src_x = 0;            // edx → CellStruct.x（int16）
  std::int16_t src_y = 0;            // edx → CellStruct.y
  std::int32_t workv = 0;            // 栈参1（[esp+4]）
  std::int32_t layer = 0;            // 栈参2（[esp+8]）层高调整，==-1 跳过
  std::int32_t w_bound = 0;          // [0xABED04] = W
  std::int32_t w2h_bound = 0;        // [0xABED08] = W+2H
  std::int32_t side = 0;             // [0x89C2DC]（work 索引用）
};

// 0x5A6C10。零引用死代码，语义按黄金逐条复刻。返回 0（EAX 无意义）。
int DeadCode6C10(const DeadCode6C10Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_6C10_H_