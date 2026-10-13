#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x568300: MapClass::CoordinatesLegal(CellStruct)——等距菱形坐标合法性。
// 与死代码区 0x5A7250/0x5A76E0 的菱形谓词同构（[0xABED04]=W、[0xABED08]=W+2H
// 即从这里缓存出去）。坐标按 int16 符号扩展比较。
bool map_coordinates_legal(int map_w, int map_h, std::int16_t x, std::int16_t y);

}  // namespace ra2::runtime