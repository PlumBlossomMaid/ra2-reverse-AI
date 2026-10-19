// 死代码复现 —— 0x5A6D80 / 0x5A7070 定向步进（batch 930）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A6D80（752B）/ 0x5A7070（471B）
// 调用约定：thiscall 无 this，ret 0x10 四栈参（栈参1 = 输出坐标指针、
//   栈参2 = 参考坐标 CellStruct*（兼初始坐标源）、栈参3 = 区域 {x,y,w,h}、
//   栈参4 = 方向参数）。零引用死代码（TSV:1026/1027，batch 322 已闭语义）。
//
// 语义链（完整汇编逐段核对 + golden 5 用例实证，deadcode_stepdir_golden.txt）：
//
// 0x5A6D80（定向步进 + 命中分支 + 距离采样 + 反向补步）：
//   · 档位：direction==2 → (edi,ebx)=(1,3)、[esp+0x1c]=-3、[esp+0x18]=6、
//      ebp=0、off14=0；direction!=2 → (edi,ebx)=(3,1)、ebp=-3、off14=6。
//   · 主循环：X>=w+x 或 Y>=h+y → 写 0 返回（出界）；GetCellAt 读 cell+0x38
//     == [0xABC2B0] 命中 → 0x5A6E99 分支；否则按 方向&7 查 0x89F688
//     方向表步进（★方向表在 .data 未初始化区=真机 0 → 死循环，golden 灌
//     DIR8 标准表，见怪癖④）。
//   · 命中分支：判界出界写 0；组装矩形 {X+ebp, Y+off1c, edi+off14} →
//     call 0x5A7250（打桩，golden azn 系列）；al 非零 → 成功路径；零 →
//     方向表步进后回 0x5A6E9D 复用判界。
//   · 成功路径（0x5A6F55）：esi = max(Y-refY, X-refX)；esi<=0 → 写当前
//     坐标（hit-ok：起点=参考点距离 0 直写）；否则拒绝采样
//     n = ftol(rand * (esi+1) * 2^-32)，n>esi 重抽（ja 无符号）；
//     n<=0 → 写当前坐标；n>0 → 反向补步：(direction-4)&7 方向表逐格
//     X/Y += 方向，每步组装矩形 {X'+ebp, Y'+[esp+0x20], edi, ebx} 调
//     0x5A7250，al==0 中止写当前坐标，n 走完写最终坐标。
//
// 0x5A7070（正向步进 + 反向随机补步）：
//   · 档位：direction==2 → (esi,edi)=(1,3)、off14=-3、off1c=6；
//     !=2 → (3,1)、off10=-3、off18=6。
//   · 主循环：判界带 +1（X>=w+x+1 出界，区别于 6D80）；组装矩形
//     {X+offxx, Y+offyy, esi+offxx, edi+offyy} 每步调 0x5A7250，al==0
//     → 0x5A7192 补步分支；al!=0 → 方向表 (direction&7) 步进。
//   · 补步分支：idx=(direction-4)&7，目标 = 当前 + 方向[idx]；
//     距离 = max(目标X-refX, 目标Y-refY)；<=6 → 直写目标（不吃 RNG）；
//     >6 → 拒绝采样 n = ftol(rand * 4/2^32)，n>3 重抽；n<=0 直写；
//     n>0 → 沿 idx 逐格补 n 步写最终坐标。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 方向表 [0x89F688] 在 .data 未初始化区（PE raw 范围外）= 真机全 0：
//     两个函数主循环靠它步进 → 真机若被调用会死循环（batch 928 单次
//     合成坐标不受影响，本批必须灌 DIR8 才能收敛——golden 已灌）。
//   ② [0xABC2B0] 同为 .data 未初始化（=0）：cell+0x38（砖号）默认也 0，
//     撞上即命中分支（golden hit-* 用例 abcb2b0=0 全图命中）。
//   ③ 0x5A7250 判定在 golden 打桩（azn/one_shot 控制返回序列），C++ 侧
//     ProbeArea 回调同桩语义。真身语义（菱形界 + cell 谓词链）batch 222
//     已登记，本批不过度引（主循环步进/距离采样/补步才是本批语义主体）。
//   ④ 成功路径距离用 max(Y-refY, X-refX) 有符号比较取大（esi = eax>esi
//     时换），非绝对值。
//
// 常量（.rdata 取证）：[0x7ED898] = 2^-32×(1+2^-40)（采样缩放）、
//   [0x7EDA38] = 4/2^32×(1+2^-40)（0..3 拒绝采样比例，7070 补步）。
#ifndef RA2_RMG_DEAD_CODE_STEPDIR_H_
#define RA2_RMG_DEAD_CODE_STEPDIR_H_

#include <cstdint>

#include "map_grid.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 方向表（golden 灌装值，见怪癖①；与 batch 928 对 [0x89F688] 处理一致）
// 表项 = {dx, dy} int16 × 8。（真机 .data 未初始化 = 0，golden 为收敛灌
// 标准八方向；C++ 侧按 golden 同表复刻。）
struct Dir8Entry {
  std::int16_t dx;
  std::int16_t dy;
};

// 区域结构（栈参3）：{x, y, w, h}
struct StepDirRegion {
  int x, y, w, h;
};

// 这一阶段要问外界的事（被调族按 golden 桩语义抽象）：
class DeadStepDirWorld {
 public:
  virtual ~DeadStepDirWorld() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（拒绝采样）
  virtual Randomizer& Rng() = 0;

  // —— GetCellAt(0x5657A0) → cell+0x38 ——
  // 0x5A6D80 命中检测用（0x5A7070 不用）。golden 侧 TechOracle cell 数组
  // 真身，C++ 侧同语义查询 + [0xABC2B0] 目标值在模型外比较。
  virtual std::uint32_t CellOwner(int x, int y) = 0;

  // —— 0x5A7250 判定（打桩） ——
  // 矩形三 dword（golden ZN 行记录 + 1 栈参）。golden 侧返回按用例
  // azn/one_shot 序列；C++ 侧 ProbeArea 实现同一序列语义。
  virtual bool ProbeArea(int a, int b, int c, int d) = 0;
};

struct DeadStepDirContext {
  Randomizer* rng = nullptr;
  DeadStepDirWorld* world = nullptr;
  StepDirRegion region;   // 栈参3
  int direction = 0;          // 栈参4
  std::uint32_t abc_b2b0 = 0; // [0xABC2B0] 命中目标（cell+0x38 比对）
  const Dir8Entry* dir8 = nullptr;   // 方向表（golden DIR8）
};

// 0x5A6D80。返回 al（EAX&0xFF），out = 最终坐标 dword
// （低 16 = X、高 16 = Y；出界写 0）。
std::uint32_t DeadCodeStepDir6D80(const DeadStepDirContext& ctx,
                                   int refx, int refy, CellCoord begin,
                                   std::int32_t* out_al);

// 0x5A7070。同上。
std::uint32_t DeadCodeStepDir7070(const DeadStepDirContext& ctx,
                                   int refx, int refy, CellCoord begin,
                                   std::int32_t* out_al);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_STEPDIR_H_