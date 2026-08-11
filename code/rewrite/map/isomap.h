// IsoMapPack5 编码 + LZO Pipe 块打包 + UUBlock 行 —— 逐位复刻 gamemd.exe。
//
// 地址依据（Ghidra 反编译 + 汇编 + oracle 实测）：
//   0x56B3F0 IsoMapPack5 编码器（rmg_isomap_enc.txt）：
//     遍历 MapClass Cells 数组（VectorMax=0x40000 槽，i 从 1 起、NULL 跳过），
//     每条非默认记录 11 字节：[coord u32 (Y<<16)|X][+0x38 IsoTileType u32]
//     [+0x11A Height u8][+0x11B Level u8][+0x119 unknown u8]，
//     末尾写默认坐标哨兵 [0 u32]。
//     过滤条件（汇编 0x56B44A-0x56B4xx）：InMap'（用 MapRect.W=W+4 / H+12）：
//       X+Y > W && X-Y < W && Y-X < W && X+Y <= W+2H；
//       coord != 默认 0（0xABD480）；cell != InvalidCell（0xABDC50）；
//       iso != 0xFFFF || level > 0。
//   0x55C2B0/0x55C350/0x55C5E0/0x55C310 LZOPipe（rmg_isomap_pipe.txt + 汇编）：
//     数据累积缓冲 +0x14，满块 0x2000 → LZO 压缩（0x55BB90）→ 写块
//     [u16 压缩长][u16 原长][LZO1X 数据]；Flush（vtable+4）压缩尾部剩余（同格式）。
//   0x526E80 WriteUUBlock（uublock_syms.txt + 汇编 0x4AD9D5）：base64 流
//     每 0x46=70 字符一行，key = 行号（"%d" 从 1 起）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mapenc {

// LZO Pipe 块打包（编码方向）：raw 按 0x2000 分块，每块
// [u16 压缩长][u16 原长][LZO1X 数据]，尾部剩余同样式。
// 对应 LZOPipe::Put + Flush（0x55C350 / 0x55C5E0 编码路径）。
std::vector<uint8_t> LzoPipeCompress(const std::vector<uint8_t>& raw);

// WriteUUBlock 行分割：base64 编码后每 70 字符一行。
// 行 key 由调用方生成（"1"、"2"…）。
std::vector<std::string> UUBlockLines(const std::vector<uint8_t>& data);

// 一条 IsoMapPack5 记录（对应 CellClass 的打包坐标 + 4 个字段）。
struct IsoCell {
  uint16_t x, y;        // 等距坐标，写入时打包 (y<<16)|x
  uint32_t isoTile;     // CellClass +0x38
  uint8_t height;       // +0x11A
  uint8_t level;        // +0x11B
  uint8_t unknown;      // +0x119
};

// IsoMapPack5 编码：遍历 slots（VectorMax 项，NULL=空槽），过滤后生成
// 11B 记录流 + 默认坐标哨兵，经 LZO Pipe 输出块流。
// w/h = MapRect.W/H（生成器 Set_Dimensions 传 W+4 / H+12）。
std::vector<uint8_t> IsoMapPack5Encode(const std::vector<const IsoCell*>& slots,
                                       uint32_t vector_max, int w, int h);

}  // namespace mapenc
