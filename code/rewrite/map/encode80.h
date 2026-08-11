// OverlayPack 编码：encode80（F80 格式的分段 RLE）——逐位复刻游戏 .map 写出链。
//
// encode80 是 RA2 的 overlay 稀疏编码（游戏读 OverlayPack 用 decode5/80 家族）。
// 参考实现：EA 官方任务编辑器 GPL-3.0 仓库 MissionEditorPackLib 的 3rdParty/xcc
// （shp_decode.cpp encode80），语义已按 RA2 游戏兼容性验证。
//
// 算法要点（XCC 版 encode80 的逐条语义）：
//   · 四种命令 + 一种结束码（见 kCmdXxx 注释）
//   · 每步先数「原地 run」（相同字节连续）与「前缀区最长匹配」，
//     匹配更长且 >2 就发 copy 命令，否则攒 raw 或发 fill
//   · copy 命令只允许相对偏移（r-p <= 0xFFF 时）或绝对偏移（p-s）
//   · 结束码 0x80（bit7=1, bit6=0, count=0）
//
// F80 外壳（EA 编辑器 EncodeF80）：把整表切成 nSections 段，每段
// [3B 压缩长 LE][1B 0x20][encode80 数据]。游戏侧段数由 e2e 对拍校准
// （编辑器用 32 段 / 0x40000 项）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mapenc {

// encode80：把 cb_s 字节压缩到 d（调用方保证 d 容量 >= cb_s）。
// 返回压缩后的字节数。
std::size_t Encode80(const std::uint8_t* s, std::uint8_t* d, std::size_t cb_s);

// F80 外壳：data 切成 n_sections 段，每段 [3B len][0x20][encode80 数据]。
// 调用方保证 len = data.size() 能被 n_sections 整除（原版 262144/32）。
std::vector<std::uint8_t> EncodeF80(const std::vector<std::uint8_t>& data,
                                    std::size_t n_sections);

}  // namespace mapenc
