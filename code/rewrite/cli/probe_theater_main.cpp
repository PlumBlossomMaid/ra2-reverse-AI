// 打印 CLI 解析的 TheaterData（对照取证表 tileset_starts.txt）
#include <cstdio>
#include <string>

#include "rmg/theater.h"
#include "assets/theater_assets.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: probe_theater <game_dir>\n");
    return 1;
  }
  ra2::assets::TheaterAssets assets;
  std::string err;
  for (int t = 0; t < 2; ++t) {
    const ra2::assets::TheaterAssets::Theater th =
        (t == 0) ? ra2::assets::TheaterAssets::kTemperate
                 : ra2::assets::TheaterAssets::kSnow;
    if (!assets.Load(argv[1], th, &err)) {
      std::fprintf(stderr, "theater[%d] load failed: %s\n", t, err.c_str());
      continue;
    }
    const ra2::rmg::TheaterData& d = assets.data();
    std::printf("== theater[%d] total_tiles=%d sets=%d\n", t, d.total_tiles,
                (int)d.sets.size());
    for (const ra2::rmg::TileSetInfo& s : d.sets) {
      std::printf("  set %3d: [%4d..%4d) n=%3d\n", s.index, s.start,
                  s.start + s.tiles_in_set, s.tiles_in_set);
    }
    std::printf("  water_set=%d shore_pieces=%d green_tile=%d clear_to_green_lat=%d\n",
                d.water_set, d.shore_pieces, d.green_tile, d.clear_to_green_lat);
    std::printf("  clear_tile=%d rough_tile=%d sand_tile=%d clear_to_sand_lat=%d\n",
                d.clear_tile, d.rough_tile, d.sand_tile, d.clear_to_sand_lat);
    std::printf("  waterfall_east=%d west=%d north=%d south=%d\n",
                d.waterfall_east, d.waterfall_west, d.waterfall_north,
                d.waterfall_south);
    std::printf("  water_cliffs=%d ramp_base=%d bridge_set=%d black_tile=%d\n",
                d.water_cliffs, d.ramp_base, d.bridge_set, d.black_tile);
    std::printf("  cliff_set=%d cliff_ramps=%d ramp_smooth=%d water_caves=%d\n",
                d.cliff_set, d.cliff_ramps, d.ramp_smooth, d.water_caves);
    std::printf("  wood_bridge_set=%d destroyable_cliffs=%d\n",
                d.wood_bridge_set, d.destroyable_cliffs);
    std::printf("  pave_tile=%d misc_pave_tile=%d paved_roads=%d paved_road_ends=%d\n",
                d.pave_tile, d.misc_pave_tile, d.paved_roads, d.paved_road_ends);
    std::printf("  slope_set_pieces=%d\n", d.slope_set_pieces);
  }
  return 0;
}
