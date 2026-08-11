// LZO1X 压缩/解压 —— 逐位复刻 gamemd.exe 内嵌实现。
//
// 地址依据（Ghidra 反编译 memory/data/decomp/rmg_lzo_core.txt / rmg_lzo_core2.txt /
// rmg_lzo_dec_b64core.txt + 汇编 rmg_lzo_dec_asm.txt）：
//   0x55BB90 LZO 压缩入口（__fastcall ecx=src, edx=src_len; 栈 dst, &dst_len, work）
//   0x55BC00 LZO 压缩核心（滚动哈希 + M1/M2/M3/M4 编码）
//   0x55C0E0 LZO 解压（__fastcall ecx=src, edx=src_len; 栈 dst, &dst_len）
//
// 与官方 lzo1x 的差异（本实现复刻 YR 行为，不是通用 LZO）：
//   - 匹配扩展失败（仅 3 字节前缀命中）时 M1/M2/M4 把"字面量+2字节匹配"合并编码
//   - M1 编码 dst[0]=((off-1)&7)<<2 | (lit+7)*0x20（8 位截断）、dst[1]=(off-1)>>3
//   - M2 短距离（off<=0x4000）：dst[0]=(len-2)|0x20，偏移 2 字节
//   - M3 远距离（off>0x4000）：dst[0]=((off-0x4000)>>0xb)&8 | (len-2) | 0x10
//   - 长字面量 0x00 + 0x80808081 魔术数计数（0xFF 一组的余数编码）
//   - 哈希索引 (dv*0x9F5F>>5)&0x3FFF，距离上限 0xBFFF，滚动 (src[i+3]<<10 ^
//     (src[i]>>5) ^ (dv>>5))
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mapenc {

// LZO 压缩。work 为 16384*4 字节哈希表（调用方清零）。返回压缩字节数；
// dst 容量由调用方保证（原版 Pipe 缓冲 0x4000，压缩比最坏略大于 1:1）。
std::vector<uint8_t> LzoCompress(const uint8_t* src, size_t src_len);

// LZO 解压。返回解压字节数；dst 容量由调用方保证。
size_t LzoDecompress(const uint8_t* src, size_t src_len, uint8_t* dst,
                     size_t dst_cap);

}  // namespace mapenc
