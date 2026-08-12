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
  const int probes[] = {0, 131, 132, 133, 134, 143, 145, 493, 494, 510, 511,
                        512, 513, 514, 515, 516, 517, 518, 519, 520, 521,
                        419, 420, 29, 30, 49};
  for (int t : probes) {
    const ra2::rmg::TileType& tt = tiles[(std::size_t)t];
    std::printf("tile %d: w=%d h=%d sub=%d", t, tt.width, tt.height,
                (int)tt.cells.size());
    if (!tt.cells.empty()) {
      std::printf(" sub0: present=%d ramp=%d terrain=%d", tt.cells[0].present,
                  tt.cells[0].ramp, tt.cells[0].terrain_type);
    }
    std::printf("\n");
  }
  return 0;
}
