// 温和气候（TEMPERAT）的 Morphable（TileTypeClass +0x2E0）真值表。
//
// 取证：游戏内存 [0xA8ED28] DVC（items=0x17CEAAB8, count=838）逐项读
// +0x2E0，115 项非零。数据源是**运行时的砖表**（.tmp 装载后），
// 不来自 theater ini / rulesmd.ini / .tmp 文件头——那些文件里没有这个标志。
// 雪地（SNOW）的集合未取证，沿用本表会错误（水/岸保护区间不同），
// 接入前须补 snow 的 dump。
//
// 语义（0x6B2520，造山可变形判定）：
//   if (tile < count && tile != 0xFFFF) return TileTypes[tile]->+0x2E0 != 0;
// morphable=true 的砖才能被 CreateHills 抬升/贴坡道；水砖（314-327）、
// 岸砖、多数地形砖 morphable=false，造山时原样保留。
#ifndef RA2_RMG_MORPHABLE_TILES_H_
#define RA2_RMG_MORPHABLE_TILES_H_

#include <array>

namespace ra2 {
namespace rmg {

// 115 个 morphable=true 的砖号（其余 723 个都是 false）。
inline constexpr std::array<int, 115> kMorphableTemperate = {
    // 0-48：草地/平原系
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,
    13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,
    26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,
    39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
    // 131-147
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
    143, 144, 145, 146, 147,
    // 404-413
    404, 405, 406, 407, 408, 409, 410, 411, 412, 413,
    // 510-533（山坡 512-529 在内）
    510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521,
    522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533,
    // 551-565
    551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562,
    563, 564, 565,
};

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_MORPHABLE_TILES_H_
