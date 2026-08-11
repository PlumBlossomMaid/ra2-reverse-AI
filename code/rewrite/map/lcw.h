// LCW 压缩/解压 —— 逐字节复刻 gamemd.exe 内嵌实现（Westwood LCW）。
//
// 地址依据（Ghidra 反编译 memory/data/decomp/rmg_lcw_core.txt +
// 汇编 rmg_lcw_dec_asm.txt 逐指令核对）：
//   0x551E50 LCW 压缩（__fastcall ecx=src, edx=dst; 栈 src_len）
//   0x551C60 LCW 解压（__fastcall ecx=src, edx=dst; 返回解压字节数）
//
// 格式（命令字节首字节决定）：
//   0x00-0x7F 短复制  [cmd][off]        len=(cmd>>4)+3, off=((cmd&0xF)<<8)|off
//   0x80      流结束
//   0x81-0xBF 字面量  [cmd][data...]    len=cmd&0x3F
//   0xC0-0xFD 中复制  [cmd][off_lo][off_hi]  len=(cmd&0x3F)+3, off=LE16
//   0xFE      填充    [FE][count_lo][count_hi][fill]  count=LE16
//   0xFF      长复制  [FF][len_lo][len_hi][off_lo][off_hi]  len/off=LE16
//
// 压缩器语义要点（与汇编一一对应）：
//   - 首输出固定 0x81 + src[0]（空输入时越界读缓冲 0）
//   - run 检测：*p == p[0x40] 才进入；repe scasb 统计连续字节数-1 作 FE count
//   - 匹配查找：repne scasb 从滚动 base 找 *p，p[ml-1]==候选[ml-1] 才扩展
//   - 短复制 off 是相对距离（p-prev_match），中/长复制 off 是绝对偏移
//   - 字面量命令 0x80..0xBF 累计，满 0xBF 开新命令
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mapenc {

// LCW 压缩。返回压缩字节数。输入缓冲按 YR 引擎语义在尾部补 0（压缩器
// 会越界读 p[0x40]，0x40 字节哨兵足够）。dst 容量调用方保证。
std::vector<uint8_t> LcwCompress(const uint8_t* src, size_t src_len);

// LCW 解压。返回解压字节数；dst 容量由调用方保证。
size_t LcwDecompress(const uint8_t* src, size_t src_len, uint8_t* dst,
                     size_t dst_cap);

}  // namespace mapenc
