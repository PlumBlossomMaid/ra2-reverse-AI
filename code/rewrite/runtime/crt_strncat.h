// _strncat（窄字符 strncat）逐字节复刻模型 —— batch 1030
//
// 0x7CB550(291B) = MSVC6 CRT _strncat——cdecl 3 栈参 (dst, src, count)，
// 返回 dst；语义 = 找 dst 尾 NUL → 从 NUL 处追加 src 最多 count 字符 →
// 恒补 NUL（count 耗尽 or src NUL 都补）。全程零子调用零全局。
//
//   ★机器码全段（disasm 复核，batch 1030）：
//     0x7CB550: mov ecx,[esp+0xC]=count → test ecx,ecx → je 0x7CB604
//       （★★count==0 早退——直接返 dst，不写任何东西——与 wcsncat
//        count==0 写 NUL 不同）
//     mov edi,[esp+8]=dst → 逐字节扫尾（未对齐）：
//       al=[edi]; inc edi; test al,al → je 0x7CB5AB(dec edi 收尾)
//       test edi,3 → jne 循环（直到 4 对齐）
//       对齐后 4 字节窗口（0x7CB57A）：
//         eax=[edi]; edx=0x7EFEFEFF; add edx,eax; xor eax,-1; xor eax,edx
//         add edi,4; test eax,0x81010100 → je 窗口循环（无 NUL）
//         命中回读 [edi-4]：test al → sub edi,4（byte0）；test ah →
//         sub edi,3（byte1）；test 0xFF0000 → sub edi,2（byte2）；
//         test 0xFF000000 → jne 继续（byte3 非 NUL）；否则 dec edi
//    复制（0x7CB5BB）：esi=[esp+0x14]=src → 未对齐慢复制逐字节：
//       dl=[esi]; inc esi; test dl,dl → je 0x7CB60A（写 NUL 终止）
//       mov [edi],dl; inc edi; dec ecx → je 0x7CB600（写 NUL）
//       test esi,3 → jne 慢循环；对齐后 dword 复制（0x7CB614）：
//         mov [edi],edx; add edi,4; dec ecx → je 0x7CB5EC（尾字节）
//         0x7CB61C 窗口检 src NUL（0x7EFEFEFF 魔术）：命中 dl/dh/
//         0xFF0000/0xFF000000 判定 → 回退按字节写并终止
//       尾字节（0x7CB5EC）：and ecx,3 → je 0x7CB600；循环逐字节
//         复制直到 NUL（je 0x7CB602 自然终止）或 count 耗尽
//     0x7CB600: mov [edi],cl（★count 耗尽补 NUL——ecx 此时 0）
//     0x7CB602/0x7CB604: mov eax,[esp+8]=dst → pop → ret
//
//   ★怪癖（与 batch 978 wcsncat 对照）：
//     ① ★count==0 早退不写任何东西（0x7CB557 je → mov eax,[esp+8]
//        → ret）——wcsncat 则写 NUL；两者行为不同照抄各自；
//     ② 恒补 NUL：count 耗尽路径 0x7CB600 写 [edi]=cl（ecx 已 0）；
//        src NUL 路径把 NUL 复制进 dst（慢路径 0x7CB60A mov [edi],
//        dl；dword 路径按命中字节位回退写含 NUL 的 word/dword）；
//     ③ 扫尾/复制都用 0x7EFEFEFF 魔术做 4 字节窗口 NUL 检测
//        （big-endian 0x01010100 判定——batch 964 strcmp 同款
//        FAST 路径）；未对齐先逐字节到 4 边界；
//     ④ 返回 dst 从 [esp] 重读（mov eax,[esp+8]）——非保留寄存器；
//     ⑤ dword 复制中 src NUL 命中 → 回退按字节写（0x7CB658 word、
//        0x7CB668 word、0x7CB64E dword）——部分写含 NUL 提前终止。

#ifndef RA2_RUNTIME_CRT_STRNCAT_H_
#define RA2_RUNTIME_CRT_STRNCAT_H_

#include <cstddef>

namespace ra2::runtime {

// _strncat 逐字节复刻：把 src 最多 count 个字符追加到 dst 尾部 NUL
// 处并恒补 NUL；count==0 时 dst 完全不动；返回 dst。
// ★前提：dst 必须是合法 C 字符串（有 NUL 结尾）；src 可为任意截断
// 字节串。未对齐/对齐快慢路径的选择不影响可观测结果（写入字节序
// 与终止位置一致），模型按语义等价实现、怪癖注释照抄。
char* strncat_legacy(char* dst, const char* src, std::size_t count);

}  // namespace ra2::runtime

#endif  // RA2_RUNTIME_CRT_STRNCAT_H_