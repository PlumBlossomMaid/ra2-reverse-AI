// 打印 CLI 解析的 TileType 表关键砖状态（对照游戏内存 _tile_sub0.txt）
#include <cstdio>
#include <string>

#include "assets/theater_assets.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: probe_tiles <game_dir>\n");
    return 1;
  }
  ra2::assets::TheaterAssets assets;
  std::string err;
  if (!assets.Load(argv[1], ra2::assets::TheaterAssets::kTemperate, &err)) {
    std::fprintf(stderr, "load failed: %s\n", err.c_str());
    return 1;
  }
  std::vector<ra2::rmg::TileType> tiles;
  assets.BuildRmgTileTable(&tiles);
  std::printf("total=%d\n", (int)tiles.size());
  const ra2::rmg::TheaterData& d = assets.data();
  struct Range { int lo; int hi; };
  const Range ranges[] = {
      {d.cliff_set, d.cliff_set + 0x28},
      {d.slope_set_pieces, d.slope_set_pieces + 10},
      {d.paved_roads, d.paved_roads + 0x15},
      {d.paved_road_ends, d.paved_road_ends + 4},
  };
  for (const Range& r : ranges) {
    std::printf("== range %d..%d\n", r.lo, r.hi);
    for (int t = r.lo; t < r.hi; ++t) {
      const ra2::rmg::TileType& tt = tiles[(std::size_t)t];
      std::printf("tile=%d w=%d h=%d sub=%d", t, tt.width, tt.height,
                  (int)tt.cells.size());
      for (std::size_t k = 0; k < tt.cells.size(); ++k) {
        std::printf(" [%d]%c%d/%d", (int)k, tt.cells[k].present ? 'p' : '-',
                    tt.cells[k].height, tt.cells[k].ramp);
      }
      std::printf("\n");
    }
  }
  return 0;
}
