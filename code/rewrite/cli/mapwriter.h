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
// 坐标语义（map-output-format.md §坐标）：Waypoints/Terrain/Structures 的
// x/y 一律是**等距坐标**（cell 列/行，范围 [0, W+H]），不是真实坐标——
// 本机官方图实锤：sandwell（100×150）waypoint (229,148)、Terrain (127,217)，
// 捡箱子（200×200）waypoint (374,203)/(202,364) 全部远超地图尺寸；
// FA2 PosToXY 后按 x + y*IsoSize 当 cell 索引用（MapData.cpp:2979）。
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
