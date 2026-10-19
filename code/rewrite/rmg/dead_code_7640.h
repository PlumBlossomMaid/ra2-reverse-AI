// 死代码复现 —— 0x5A7640 点集 bounding box 计算器（batch 936）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A7640-0x5A76D4（149B）
// 调用约定：ret 0（无栈参），ecx = 输出矩形指针（4×int32）、edx = 输入
// 对象指针（+4 = 元素数组、+0x10 = 元素个数 n）。零引用死代码
// （TSV:0x005A7640 未登记；2026-10-19 全段汇编复核 + golden 8 用例实证，
// deadcode_7640_golden.txt）。2 unique_callers = 0x5A5360 / 0x5A5B50
// （废弃方案大函数的开场，见批文档 321/936）。
//
// 语义链（0x5A7640-0x5A76D4 逐段核对）：
//   · 变量（16 位寄存器）：di=minX 初值 999(0x3E7)、bx=minY 初值 999、
//     ax=maxX 初值 0、bp=maxY 初值 0（0x5A7643/0x5A764D/0x5A7651/
//     0x5A7655）。
//   · 主循环（0x5A7658-0x5A76AC，★从数组末尾往前遍历）：
//       读元素 dword（0x5A766D mov edx,[edx]）：低 16 = X、高 16 = Y
//       （0x5A766F cmp dx,di; jge 跳过 → minX = min(minX, X)，16 位
//       有符号比较）
//       （0x5A767B mov si,[esp+0x1A] 读高 16 = Y; cmp si,bx; jge 跳过
//       → minY = min(minY, Y)）
//       （0x5A7688 cmp dx,ax; jle 跳过 → maxX = max(maxX, X)）
//       （0x5A7690 cmp si,bp; jle 跳过 → maxY = max(maxY, Y)）
//       指针 -= 4、计数--（0x5A76A0/0x5A76A3）、jne 循环。
//   · 输出段（0x5A76AE-0x5A76CB）：movsx 把 16 位 min/max 符号扩展成
//     32 位：
//       out[0] = (s32)minX；out[1] = (s32)minY；
//       out[2] = (s32)maxX - (s32)minX + 1 = w；
//       out[3] = (s32)maxY - (s32)minY + 1 = h。
//   · RNG 不参与；无 GetCellAt / 谓词依赖——纯数组扫描。
//
// ★原版怪癖（照抄，golden 8 用例实证）：
//   ① min 初值 999、max 初值 0 —— 空数组（n==0）时输出
//     {999, 999, 0-999+1=-998, 0-999+1=-998} 哨兵（golden empty）；
//   ② 全负坐标时 max 保持初值 0（-1 > 0 为假，jle 跳过）——max 不是
//     「真实最大值」（golden allneg：{ -2,-2, 3,3}，max 恒 0）；
//   ③ X/Y 各自独立扫描，元素布局 = {short X, short Y} 4B（低 16 X、
//     高 16 Y），读取后各自参与 16 位有符号比较；
//   ④ 遍历从数组末尾往前（lea [edx+esi*4] 指向 arr[n-1]），顺序不
//     影响 min/max；
//   ⑤ 输出 w/h = max-min+1（含端点），16→32 符号扩展（movsx）。
//
// 对拍点（golden 8 用例，deadcode_7640_golden.txt）：输出矩形 4 个
// int32 {minX, minY, w, h}。用例覆盖：empty（哨兵）、single（单点
// {5,7,1,1}）、multi（{0,1,9,9}）、neg（负坐标 {-3,-2,8,8}）、allneg
// （max 保持 0）、wide（16 位界 -32768..32767 → w=65536）、ff
// （0xFFFF 字节 = -1）、mixed。
#ifndef RA2_RMG_DEAD_CODE_7640_H_
#define RA2_RMG_DEAD_CODE_7640_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事：只有输入数组的按序读取（原版直接读内存 dword，
// 抽象成按索引读元素以便对拍）。
class DeadCode7640World {
 public:
  virtual ~DeadCode7640World() {}

  // 读第 i 个元素（i ∈ [0, count)），返回 dword：低 16 位 = X（int16）、
  // 高 16 位 = Y（int16）。原版 0x5A766D mov edx,[edx] 读整个元素。
  virtual std::uint32_t Element(std::int32_t i) = 0;
};

struct DeadCode7640Context {
  DeadCode7640World* world = nullptr;
  std::int32_t count = 0;   // [obj+0x10] 元素个数
};

// 0x5A7640。零引用死代码，语义按黄金逐条复刻。写 out[0..3] =
// {minX, minY, w, h}（w/h = max-min+1）。无失败路径（恒成功）。
void DeadCode7640(const DeadCode7640Context& ctx, std::int32_t out[4]);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_7640_H_