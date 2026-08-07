#pragma once

#include <cstdint>

#include "randomizer.h"

namespace ra2::rmg {

// 原版全局 x87 状态（复刻任何浮点逻辑的前提）
//   WinMain @0x6BBFB7: push 0x300 / push 0x300 / call 0x7CBF49(_control87)
//     -> _control87(_RC_CHOP, _MCW_RC)：舍入模式 = 向零截断
//   紧接着 @0x6BBFC9 call 0x7C5EE4 把当时的控制字缓存进 [0x822D80]，实测值 0x0E7F
//     = PC 位 0b10（53 位双精度）+ RC 位 0b11（chop）+ 全部异常屏蔽
//   _ftol @0x7C5F00 走快路径的条件就是当前控制字 == [0x822D80]，
//   即原版整个生命周期里浮点运算都在「双精度 + 向零截断」下进行。
// 不进入同一模式，原版的浮点取数惯用法会在边界值上与我们分岔。
class FpuChopScope {
 public:
  FpuChopScope();
  ~FpuChopScope();

  FpuChopScope(const FpuChopScope&) = delete;
  FpuChopScope& operator=(const FpuChopScope&) = delete;

 private:
  unsigned int saved_;
};

// 地图生成器的取数惯用法（例：0x59C70F / 0x59C79C）
//   do { v = (int)((double)(uint32)Random() * (n / 4294967295.0)); } while (v > n - 1);
// 比例常数是编译期算好的 n/(2^32-1)（已核对 exe 中 0x7ED9D8=201 / 0x7ED9E0=242 /
// 0x7ED9E8=10 三个 double 与 n/4294967295.0 的字节完全一致）。
// 只有 Random() == 0xFFFFFFFF 才会取到 n 而被拒绝，结果等价于均匀 [0, n-1]。
int PickBelow(Randomizer& rng, int n);

// 带 `fadd 1.0` 的变体（例：0x59C6DA，拒绝条件 v > n）——结果落在 [1, n]
int PickFrom1(Randomizer& rng, int n);

// 0x7ED898 = 1/(2^32-1)。整个生成器只有这一个"把 uint32 摊到 [0,1]"的常数。
inline constexpr double kInvUintMax = 2.3283064370807974e-10;

// _ftol（0x7C5F00）：向零截断。RC=chop 下与 C 的 (int) 转换语义一致。
inline int Ftol(double v) { return static_cast<int>(v); }

// **上限不折叠**的那一路拒绝采样（0x59BCAC 涂块随机起点 / 0x59AB13 MakeGrid 抽行）：
//   do { v = _ftol((double)(uint32)Random() * (double)limit * kInvUintMax); }
//   while ((unsigned)v > (unsigned)(limit - 1));
// ⚠️ 与上面的 PickBelow **不是同一个函数**：那个把 n/(2^32-1) 预先折叠成一次乘法，
// 这个是两次乘法。两者在末位会分岔，不能互相替代。原版两种写法都在用。
int PickBelowMul(Randomizer& rng, int limit);

// PickRange(lo, hi)（0x598030，__fastcall ecx=lo edx=hi）：**闭区间**，拒绝采样。
//   do { v = _ftol((double)(uint32)Random() * (double)(hi-lo+1) * kInvUintMax + (double)lo); }
//   while (v > hi);
// 岸线平滑第 3/4 遍每格都调它一次（PickRange(0,5)），是本阶段随机数消耗的大头。
// 拒绝分支概率约 2^-32，但必须保留——一旦命中就多吃一个随机数。
int PickRange(Randomizer& rng, int lo, int hi);

}  // namespace ra2::rmg
