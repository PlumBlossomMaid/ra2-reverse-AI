// .map 文件组装：把流水线产物写成 rmcache\RandMap.Map 的 INI 文本。
//
// 格式依据：
//   [Map]      stage-init.md §三（Size/LocalSize/Theater/Level 原版写进内存 INI）
//   [IsoMapPack5]  0x56B3F0 完整写链（isomap.cpp，oracle 对拍过）
//   [OverlayPack]  F80（encode80 + 分节头）——EA 编辑器 EncodeF80 语义；
//                  游戏侧段数/段头标志 e2e 对拍校准
//   [Waypoints]/[Terrain]/[Structures]  EA 编辑器格式（键 = "x"+"yyy" 补零 3 位、
//                  Structures 17 字段逗号表）
//
// 等距坐标 → 真实坐标（map-geometry.md §一）：
//   X = rx + ry + 1、Y = ry - rx + W   →   2rx = X - Y + W - 1、2ry = X + Y - W - 1
//   W 取 [0x87F8DC]（= grid 宽，含 4 格余量）。
//   真实坐标语义已由本机官方图实锤（sandwell.yrm / 捡箱子.yrm，见
//   docs/random-map/map-output-format.md）：waypoint/Terrain/Structures 全用
//   真实坐标。半数菱形格落在半整数上——整数除法（向零）的取舍 e2e 对拍校准。
#pragma once

#include <string>
#include <vector>

#include "cliworld.h"
#include "pipeline.h"
#include "rmg/map_grid.h"

namespace ra2 {
namespace cli {

// 组装 .map 文本。map_w/map_h = 真实宽高（[Map] Size 的 W/H 分量）；
// 返回完整文件内容。
std::string BuildMapFile(const PipelineResult& r, int theater, int level);

}  // namespace cli
}  // namespace ra2
