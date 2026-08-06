// 地图真实尺寸：把 .sed 里的 Width/Height「档位」换算成格数
//
// 原版 gamemd.exe 阶段 1（Init random map，0x599665-0x59974E）最开头的那段。
// 这一步是整条流水线的前提——后面每个阶段的公式里那两个 W/H 全部来自这里。
// 算法与取证见 docs/random-map/stage-init.md 三之二。
#pragma once

namespace ra2::rmg {

struct MapSize {
  int width;   // MapSeedClass +0x180，下游全局 [0x87F8DC]
  int height;  // MapSeedClass +0x184，下游全局 [0x87F8E0]

  // 工作数组的边长（0x42AC5C）：等距菱形的外接正方形边长
  int side() const { return width + height + 1; }
};

// width_step / height_step 是 .sed 里的 Width= / Height=，**档位不是格数**
// num_players 取 2..8；map_type 取 .sed 的 MapType（3/4 会跳过 1.2 夹取）。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用：原版全局 RC=chop，而线性插值那两步
//    在就近舍入下会有 420/1260 例结果不同（见 stage-init.md 三之二）。
MapSize ComputeMapSize(int width_step, int height_step, int num_players,
                       int map_type);

}  // namespace ra2::rmg
