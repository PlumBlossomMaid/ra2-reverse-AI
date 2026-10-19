#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x7CA564（111B，68 unique_callers / 4536 transitive_dependents，ranking.tsv
// 扇入度最高的未复刻 CRT 项）：vsprintfW 薄封装——转发宽字符格式化引擎后
// 补两个 NUL 字节。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 6 用例实证，
// memory/data/rmg/deadcode_7ca564_golden.txt）：
//
//   push ebp / mov ebp,esp / sub esp,0x20
//   mov eax,[ebp+8]          ; arg1 = dst
//   mov [ebp-0x18],eax
//   mov [ebp-0x20],eax       ; 槽 = dst
//   lea eax,[ebp+0x10]       ; &args（可变参起点）
//   mov dword [ebp-0x14],0x42 ; 死值 'B'（从未被读取）
//   push eax                 ; arg3 = &args
//   lea eax,[ebp-0x20]
//   push [ebp+0xC]           ; arg2 = fmt
//   mov dword [ebp-0x1C],0x7FFFFFFF ; 上限
//   push eax                 ; arg1 = &槽
//   call 0x7D21BE            ; 宽字符格式化核心
//   add esp,0xC
//   dec dword [ebp-0x1C]     ; 上限--
//   mov esi,eax              ; 保存返回值
//   js 0x7CA5A7              ; 上限为负 → 走 0x7CE18D
//   mov eax,[ebp-0x20]
//   and byte [eax],0         ; 写 NUL
//   inc dword [ebp-0x20]     ; 槽++
//   jmp 0x7CA5B4
// 0x7CA5A7: lea eax,[ebp-0x20] / push eax / push 0 / call 0x7CE18D
// 0x7CA5B4:
//   dec dword [ebp-0x1C]     ; 上限--
//   js 0x7CA5C1
//   mov eax,[ebp-0x20]
//   and byte [eax],0         ; 写第二个 NUL（槽不推进）
//   jmp 0x7CA5CE
// 0x7CA5C1: lea eax,[ebp-0x20] / push eax / push 0 / call 0x7CE18D
// 0x7CA5CE: mov eax,esi / pop esi / leave / ret
//
// ★原版怪癖（照抄）：
//   ① [ebp-0x14]=0x42（'B'）写入后从未被读取——MSVC6 编译器遗留的死值，
//      无可观测行为，C++ 侧不保留（仅此处记录）；
//   ② 上限 0x7FFFFFFF 连减两次，js 永不成立——0x7CE18D 分支实际不可达
//      （代码照抄，golden 断言 0 次调用）；
//   ③ 两个 NUL 是单字节 0（宽终止符 L'\0' 的字节布局），第一个写在
//      "核心推进后的槽"并把槽再 +1，第二个写在 +1 后的槽上、槽不动；
//   ④ 返回值原样透传核心结果（含负值），与写了几字节无关。

// 这一阶段要问外界的事（原版侧：0x7D21BE 宽字符格式化引擎 + 0x7CE18D
// 终止符写入器；C++ 侧抽象成世界回调以便对拍）。
class CrtFormatWorld {
 public:
    virtual ~CrtFormatWorld() {}

    // 0x7D21BE：把格式化结果写进 *slot 并推进 *slot，返回引擎返回值
    virtual std::int32_t WideFormat(std::uint8_t** slot, std::uint32_t fmt,
                                    std::uint32_t args) = 0;

    // 0x7CE18D：终止符写入（js 分支专用，上限 0x7FFFFFFF 下不可达）
    virtual void WriteTerminator(std::uint8_t** slot, std::uint32_t value) = 0;
};

// 0x7CA564：vsprintfW 薄封装。dst=输出缓冲、fmt=格式串、args=可变参起点。
// 返回核心引擎的返回值。
std::int32_t vsprintfw_legacy(CrtFormatWorld& world, std::uint8_t* dst,
                              std::uint32_t fmt, std::uint32_t args);

// 原版栈上初始化、从未参与计算的上限常量（golden 实证 js 永不成立）
inline constexpr std::int32_t kVsprintfwLimit = 0x7FFFFFFF;

}  // namespace ra2::runtime