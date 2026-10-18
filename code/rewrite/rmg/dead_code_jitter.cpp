#include "dead_code_jitter.h"

#include <cstring>

#include "fast_math.h"
#include "scaled_random.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ra2::rmg {

namespace {

// 原版 0x5A2ED0 的 j 链在 x87 全局 PC=53/RC=chop（向零截断）下执行：
//   t = (double)(uint64)r * 5.0          （u*5.0 精确，无需舍入）
//   t = t * kInvUintMax                  （RC=chop 向零截断 1 次）
//   return t + s                         （RC=chop 向零截断 1 次）
// C++ x64 SSE 标量运算默认 round-to-nearest，两处舍入方向不同 → golden 对拍
// 7/25 案例差 1 ULP。修法：临时把 MXCSR 舍入模式切为 RC=chop（toward-zero），
// 与 x87 0x0E7F 完全等价；volatile 防编译器重排/常量折叠。
#if defined(_M_X64) || defined(__x86_64__)
double ChopJitterChain(double u, double s) {
  const unsigned csr = _mm_getcsr();
  _mm_setcsr((csr & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO);
  volatile double t = u * 5.0;
  volatile double t2 = t * kInvUintMax;
  volatile double r = t2 + s;
  _mm_setcsr(csr);
  return r;
}
#else
double ChopJitterChain(double u, double s) {
  // 非 x86_64：无 MXCSR 控制，无法精确复刻 x87 RC=chop 的逐位结果。
  // （本项目主对拍平台为 x64，此处仅保证同一数学语义。）
  return u * 5.0 * kInvUintMax + s;
}
#endif

// 0x59D480 的 j 链（cdecl 3 栈参 ret 0xC，float 精度版）：
//   t = (double)(uint64)u * kInvUintMax          （RC=chop 舍入 1）
//   t = t * 10.0f                                （float 提升 double，10.0 精确无舍入）
//   t = t + 0.5f * (double)s32                   （RC=chop 舍入 1——0.5f 精确）
//   t = t - 0.02f * (double)(int32)arg3          （RC=chop 舍入 1——0.02f 非精确）
// C++ x64 SSE 默认 round-to-nearest → MXCSR toward-zero 复刻（同 0x5A2ED0 修法）。
#if defined(_M_X64) || defined(__x86_64__)
double ChopJitterChainF(double u, float s32, std::int32_t arg3) {
  const unsigned csr = _mm_getcsr();
  _mm_setcsr((csr & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO);
  volatile double t = u * kInvUintMax;
  volatile double t2 = t * 10.0f;
  volatile double t3 = t2 + 0.5f * static_cast<double>(s32);
  volatile double r = t3 - 0.02f * static_cast<double>(arg3);
  _mm_setcsr(csr);
  return r;
}
#else
double ChopJitterChainF(double u, float s32, std::int32_t arg3) {
  // 非 x86_64：仅保证同一数学语义（主对拍平台为 x64）。
  return u * kInvUintMax * 10.0f + 0.5f * static_cast<double>(s32) -
         0.02f * static_cast<double>(arg3);
}
#endif

}  // namespace

// 0x5A2ED0 / 0x5A4AE0 / 0x5A4FA0（三份同源码副本，仅私有 5.0 常量地址不同）：
//   cdecl 2 栈参（p1/p2 坐标指针，各 {int16 x; int16 y}），x87 返回 double。
//   dx = p1.x - p2.x  （movsx word 符号扩展后相减，差落在 int32）
//   dy = p1.y - p2.y
//   d2 = dx*dx + dy*dy（int32 imul 回绕——大坐标可回绕成负值）
//   s  = FastSqrt((double)(int32)d2)   （fild dword 有符号加载；
//                                        FastSqrt @0x4CAC40 对负数取绝对值）
//   j  = (double)(uint32)Random() * 5.0 * kInvUintMax（RC=chop 链，见上）
//   返回 s + j
double DeadCodeDistanceJitter(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                              std::int16_t bx, std::int16_t by) {
  const std::int32_t dx = static_cast<std::int32_t>(ax) - static_cast<std::int32_t>(bx);
  const std::int32_t dy = static_cast<std::int32_t>(ay) - static_cast<std::int32_t>(by);
  const std::int32_t d2 = dx * dx + dy * dy;  // int32 回绕

  const double s = FastSqrt(static_cast<double>(d2));
  const double u = static_cast<double>(static_cast<std::uint64_t>(rng.Random()));
  return ChopJitterChain(u, s);
}

// 0x59D480（137B，cdecl 3 栈参 ret 0xC，x87 返回 double——float 精度版）：
//   [esp+4]=p1 [esp+8]=p2 [esp+0xC]=arg3（int32 第三栈参）
//   dx = p1.x-p2.x ; dy = p1.y-p2.y（movsx word）；d2 = dx*dx+dy*dy（int32 回绕）
//   s32 = ChopFloat(FastSqrt((double)(int32)d2))  （fstp dword 窄化 float32
//     ——0x59D480 与 0x5A2ED0 不同：存 dword 而非 qword！）
//   u   = (double)(uint64)Random()                （fild qword 无符号扩展）
//   返回 u * kInvUintMax * 10.0f + 0.5f * (double)s32
//        - 0.02f * (double)(int32)arg3            （RC=chop 链，见上）
//   ★黄金实证推翻 docs/dead-code-functions.md:149「Random()*10.0/2³²+0.5*arg-
//     0.02*euclid」：逐字节追栈证明 [esp+0xC]（pop esi 后）= sqrt 结果 float32
//     槽、[esp+0x14]（pop esi 后）= 第三栈参 arg3——机器码是唯一权威。
double DeadCodeDistanceJitterF(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                               std::int16_t bx, std::int16_t by, std::int32_t arg3) {
  const std::int32_t dx = static_cast<std::int32_t>(ax) - static_cast<std::int32_t>(bx);
  const std::int32_t dy = static_cast<std::int32_t>(ay) - static_cast<std::int32_t>(by);
  const std::int32_t d2 = dx * dx + dy * dy;  // int32 回绕

  const double s = FastSqrt(static_cast<double>(d2));
  const float s32 = ChopFloat(s);  // fstp dword 窄化（float32）
  const double u = static_cast<double>(static_cast<std::uint64_t>(rng.Random()));
  return ChopJitterChainF(u, s32, arg3);
}

}  // namespace ra2::rmg
