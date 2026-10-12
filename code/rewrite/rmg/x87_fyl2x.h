// x87_fyl2x.h — 在 x64 上直接执行 x87 fyl2x 指令（绕过 SSE2 log）
// 原版 gamemd.exe 全程 x87 浮点，fyl2x 指令的硬件近似在 0.035% 案例
// 比数学定义差 1 ULP。NormalDeviate 用 ChopLog(ln(s)) → 1 ULP 差
// 会传播到 TruncNormal → GrowTreePatch → trees RNG 750 差异。
// 修法：在 x64 上用 VirtualAlloc 分配可执行内存，写入 x87 机器码。
#pragma once

#include <cstdint>
#include <cstring>

#include "scaled_random.h"  // kInvUintMax / ChopFloat（下方可移植实现要用）

// x87 机器码只在 **Windows x64** 上可用，两个条件缺一不可：
//   1. 调用约定是 Windows x64，第一个参数走 RCX。Linux x86_64 同样定义
//      __x86_64__，但 SysV ABI 下第一个参数走 RDI，机器码会读错寄存器，
//      算出的 ln 完全不是期望值——不能只按架构判。
//   2. 靠 VirtualAlloc/VirtualProtect 申请可执行页，这是 Win32 API。
// 其余平台（ARM、LoongArch、以及 CI 的 ubuntu-latest x86_64）一律走下方
// 文件内联的可移植实现：ChopLog 走 dd::ChopLogPortable（见 dd_log.h）。
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define RA2_X87_FYL2X_WIN64 1
#endif

#ifdef RA2_X87_FYL2X_WIN64

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

namespace ra2::rmg {

// x87 fyl2x + RC=chop + PC=53bit 机器码。
// 调用约定：RCX=double* in_out（Windows x64）
// 语义：*in_out = ln(*in_out)，使用 x87 fyl2x 指令 + RC=chop + PC=53bit
class X87Fyl2x {
 public:
  static X87Fyl2x& Instance() {
    static X87Fyl2x inst;
    return inst;
  }

  double Compute(double x) {
    double result = x;
    func_(&result);
    return result;
  }

  bool IsValid() const { return func_ != nullptr; }

 private:
  using Func = void(*)(double*);

  X87Fyl2x() : func_(nullptr) {
    // 机器码（Windows x64, RCX=double*）：
    //   sub rsp, 16               ; 48 83 EC 10
    //   fnstcw [rsp]              ; DD 3C 24        (save original CW)
    //   fninit                    ; DB E3           (reset x87 FPU)
    //   mov ax, 0x0E7F            ; 66 B8 7F 0E     (PC=53bit+RC=chop+all-masked)
    //   mov [rsp+8], ax           ; 66 89 44 24 08
    //   fldcw [rsp+8]             ; D9 6C 24 08
    //   fnclex                    ; DB E2           (clear exceptions)
    //   fldln2                    ; D9 ED           (st0=ln2)
    //   fld qword [rcx]           ; DD 01           (st0=x, st1=ln2)
    //   fyl2x                     ; D9 F1           (st0=ln2*log2(x)=ln(x))
    //   fstp qword [rcx]          ; DD 19           (store result)
    //   fldcw [rsp]               ; D9 2C 24        (restore original CW)
    //   add rsp, 16               ; 48 83 C4 10
    //   ret                       ; C3
    // ⚠️ 2026-10-09 修复：两处 fldcw 误写成 0xDB（fld xword）→ 控制字从未生效，
    // 全程跑 fninit 默认 0x037F（最近舍入）而非 chop。应为 0xD9。与 X87SpreadKey
    // 同源笔误（详见 docs/random-map/mt0-mt1-open-issues.md）。
    static const unsigned char kCode[] = {
      0x48, 0x83, 0xEC, 0x10,
      0xDD, 0x3C, 0x24,
      0xDB, 0xE3,
      0x66, 0xB8, 0x7F, 0x0E,
      0x66, 0x89, 0x44, 0x24, 0x08,
      0xD9, 0x6C, 0x24, 0x08,   // ← 0xD9 = fldcw（修复）
      0xDB, 0xE2,
      0xD9, 0xED,
      0xDD, 0x01,
      0xD9, 0xF1,
      0xDD, 0x19,
      0xD9, 0x2C, 0x24,         // ← 0xD9 = fldcw（修复）
      0x48, 0x83, 0xC4, 0x10,
      0xC3,
    };

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    code_ = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE));
    if (!code_) return;
    std::memcpy(code_, kCode, sizeof(kCode));
    DWORD oldProtect;
    VirtualProtect(code_, si.dwPageSize, PAGE_EXECUTE_READ, &oldProtect);
    func_ = reinterpret_cast<Func>(code_);
  }

  ~X87Fyl2x() {
    if (code_) VirtualFree(code_, 0, MEM_RELEASE);
  }

  unsigned char* code_ = nullptr;
  Func func_ = nullptr;
};

}  // namespace ra2::rmg

#endif  // RA2_X87_FYL2X_WIN64

namespace ra2::rmg {

// SpreadKey 的 x87 80位精度版本（仅 Windows x64 声明，实现在 .cpp 或 inline）
#ifdef RA2_X87_FYL2X_WIN64
// 在 x64 上用 x87 机器码执行 SpreadKey 的完整计算链：
//   fild draw → fmul 5.0 → fmul 1/(2^32-1) → fadd dist → fstp dword (chop)
// 中间计算在 80 位扩展精度下进行，最后 fstp 截断到 float（RC=chop）。
// 这与原版 0x5A4E88-0x5A4EA2 逐位一致。
float X87SpreadKey(double dist, std::uint32_t draw);
#else
inline float X87SpreadKey(double dist, std::uint32_t draw) {
  const double jitter =
      static_cast<double>(static_cast<std::int64_t>(draw)) * 5.0 * kInvUintMax;
  return ChopFloat(jitter + dist);
}
#endif

}  // namespace ra2::rmg
