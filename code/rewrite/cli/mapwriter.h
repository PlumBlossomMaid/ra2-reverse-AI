// .map 文件组装：把流水线产物写成 rmcache\RandMap.Map 的 INI 文本。
//
// 格式依据：
//   [Map]      stage-init.md §三（Size/LocalSize/Theater/Level 原版写进内存 INI）
//   [IsoMapPack5]  0x56B3F0 完整写链（isomap.cpp，oracle 对拍过）
//   [OverlayPack]  F80（encode80 + 分节头）——EA 编辑器 EncodeF80 语义；
//                  游戏侧段数/段头标志 e2e 对拍校准
//   [Header]    Starting-point bounds and real/screen WaypointN coordinates
//   [Waypoints]/[Terrain]/[Structures]  游戏 INI 格式（Waypoints/Terrain 键值
//                  为 x*1000+y，Structures 为 17 字段逗号表）
//
// 坐标语义（map-output-format.md §坐标）：Waypoints/Terrain/Structures 的
// x/y 一律是**等距坐标**（cell 列/行，范围 [0, W+H]），不是真实坐标。
// Waypoints/Terrain 的整数编码由 Seed=62298 RandMap.Map 逐字节对拍校准：
// 写出 x*1000+y。
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
