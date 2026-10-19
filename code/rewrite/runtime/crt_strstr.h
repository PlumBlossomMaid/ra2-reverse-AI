#pragma once

namespace ra2::runtime {

// 0x7CA4B0 MSVCRT strstr（子串查找，cdecl 2 栈参，返回命中位置指针）。
// 机器码 133B（0x7CA4B0-0x7CA52F）：
//   ecx=[esp+8]=SubStr / push edi/push ebx/push esi
//   dl=[SubStr]（首字符）/ mov edi,[esp+0x10]=Str
//   test dl,dl → je 0x7CA52A（★SubStr 空 → 返回 Str 原址）
//   dh=[SubStr+1] → test dh,dh → je 0x7CA517（★单字符路径）
//   主扫描（0x7CA4C8）：esi=Str / ecx=SubStr（重取）
//     al=[edi]（Str 游标）/ inc esi / cmp al,dl → je 命中
//     test al,al → je 返 0（Str 到 NUL 未找到）
//     mov al,[esi]/inc esi/cmp al,dl → je 命中 / test al,al → jne 循环
//   命中比对（0x7CA4EA）：al=[esi]/inc esi/cmp al,dh → jne 回扫描
//     （SubStr[1] 不匹配 → 继续找 SubStr[0]）
//   lea edi,[esi-1]（★命中起点）/ ah=[SubStr+2] → test ah,ah → je
//     0x7CA523（★SubStr 长度 2 → 返命中）
//   长串匹配（0x7CA4FB）：al=[esi]/add esi,2/cmp al,ah → jne 回扫描
//     al=[SubStr+3] → test al,al → je 0x7CA523（★长度 3 → 返）
//     ah=[esi-1]/add ecx,2/cmp al,ah → je 0x7CA4F4 继续 / jmp 回扫描
//   ★单字符路径（0x7CA517）：pop 族 → al=dl（SubStr[0]）→
//     jmp 0x7CAF36（★★直接跳进 strchr 内核——单字符 SubStr 退化
//     为 strchr(Str, SubStr[0])，编译器级跨函数共享；strchr_legacy
//     batch 954 已复刻）
//   0x7CA523: lea eax,[edi-1]（返命中位置）/ pop 族 / ret
//   0x7CA52A: mov eax,edi（SubStr 空 → 返 Str）/ pop 族 / ret
// ★怪癖：①SubStr 空 → 返回 Str 原址（非 0）；②单字符 SubStr 直接
//   jmp 0x7CAF36 借 strchr 内核；③命中位置以 SubStr[0] 所在指针返回
//   （edi-1，esi 已前移）；④Str 到 NUL 未找到 → 0；⑤长串匹配
//   SubStr[2..] 逐字符（esi+2/ecx+2 交错步进）。纯内存零子调用
//   （strchr 同族零桩）零全局（golden 全真身模拟，
//   deadcode_7ca4b0_golden.txt）。
const char* strstr_legacy(const char* str, const char* sub);

}  // namespace ra2::runtime