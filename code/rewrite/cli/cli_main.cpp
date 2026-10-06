// RA2 随机地图生成器 CLI：读 .sed → 全流水线 → 写 RandMap.Map。
//
// 用法：
//   ra2_rmg_cli --sed=RandMap.Sed --dir=E:\YRLauncher [--out=out.map]
//               [--seed=NNN] [--rules=path]
//
//   --sed      [RandomMap] 参数文件（.sed，17 键）
//   --dir      原版游戏目录（.mix/.tmp 砖库、rulesmd.ini）
//   --out      输出 .map 路径（默认 rmcache\RandMap.Map 相对当前目录）
//   --seed     覆盖 .sed 里的 Seed（-1 = 保持 .sed 值；.sed 也是 -1 时报错）
//   --rules    rulesmd.ini 路径（默认 <dir>\rulesmd.ini，没有则从 ra2md.mix 读）
//   --dump-cells      导出 cells 表（e2e 对拍用：0x40000 × 9B，见 cli_main.cpp 注释）
//   --dump-rng        导出流水线结束时的 RMG Randomizer 原版布局（e2e 对拍用）
//   --dump-stage-rng  导出各阶段结束时的 RNG 调用数（e2e 定位用）
//   --dump-ore-trace  导出阶段 8 每片矿田的 RNG 消耗（e2e 定位用）
//   --dump-tech-trace 导出阶段 7 中立科技建筑尝试（e2e 定位用）
//   --dump-arl-state  导出阶段 7 结束、阶段 8 调用前的完整工作数组和 cells
//                    （CreateOreField 输入对拍用）
//   --dump-stage-cells-prefix=PATH_PREFIX  导出关键阶段 cells 快照
//   --dump-stage-work-prefix=PATH_PREFIX   导出关键阶段 work 数组（按原版 0x50B 打包）
//   --dump-stage-work-names=a,b,c          只导这些阶段的 work（省略 = 全部阶段）
//   --dump-zone-table=FILE                 导出阶段 5 Recalc 后的 zone 表输入
//                                          （4B/格 land,level,zone，索引 CellToIndex，
//                                          与权威 beforerebuild/zone_table_4b.bin 对拍）
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "assets/theater_assets.h"
#include "cliworld.h"
#include "mapwriter.h"
#include "mix/mix_archive.h"
#include "pipeline.h"
#include "rmg/map_seed_params.h"
#include "rules_loader.h"

namespace {

void WriteCellsDump(const std::string& path,
                    const std::vector<ra2::cli::CliCell>& cells,
                    const std::vector<char>& exists) {
  std::ofstream df(path, std::ios::binary);
  if (!df) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  for (int i = 0; i < 0x40000; ++i) {
    const ra2::cli::CliCell& c = cells[static_cast<std::size_t>(i)];
    const std::uint8_t ex = (exists[static_cast<std::size_t>(i)] != 0);
    const std::uint8_t st = static_cast<std::uint8_t>(c.subtile);
    const std::uint8_t lv = static_cast<std::uint8_t>(c.level);
    const std::uint8_t rm = static_cast<std::uint8_t>(c.ramp);
    const std::uint8_t od = static_cast<std::uint8_t>(c.overlayData);
    // 菱形外格原版 ov=0（未初始化/保留），CLI 里保持 -1=0xFFFF——dump 时统一写 0
    const std::uint16_t ov = ex ? static_cast<std::uint16_t>(c.overlay) : 0;
    const std::uint16_t iso = static_cast<std::uint16_t>(c.isoTile);
    df.write(reinterpret_cast<const char*>(&ex), 1);
    df.write(reinterpret_cast<const char*>(&iso), 2);
    df.write(reinterpret_cast<const char*>(&st), 1);
    df.write(reinterpret_cast<const char*>(&lv), 1);
    df.write(reinterpret_cast<const char*>(&rm), 1);
    df.write(reinterpret_cast<const char*>(&od), 1);
    df.write(reinterpret_cast<const char*>(&ov), 2);
  }
}

// 0x40000 × (9B + 2B ground)，供 ground 逐位对拍。全库只有一个入口，其余
// WriteCellsDump 的调用点不扩（它们只喂 9B 表格的 e2e 对拍）。
void WriteGroundDump(const std::string& path,
                     const std::vector<ra2::cli::CliCell>& cells,
                     const std::vector<char>& exists) {
  std::ofstream df(path, std::ios::binary);
  if (!df) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  for (int i = 0; i < 0x40000; ++i) {
    const ra2::cli::CliCell& c = cells[static_cast<std::size_t>(i)];
    const std::uint8_t ex = (exists[static_cast<std::size_t>(i)] != 0);
    const std::uint16_t gnd = static_cast<std::uint16_t>(c.ground);
    df.write(reinterpret_cast<const char*>(&ex), 1);
    df.write(reinterpret_cast<const char*>(&gnd), 2);
  }
}

// 导出阶段 5 Recalc 后的 zone 表输入（4B/格 land,level,zone），与权威
// beforerebuild/zone_table_4b.bin 对拍。索引 = CellToIndex(y*side+x)。
void WriteZoneTableDump(const std::string& path, int side,
                        const std::vector<ra2::rmg::ZoneCell>& zt) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  const int n = side * side;
  for (int i = 0; i < n; ++i) {
    const ra2::rmg::ZoneCell& c = zt[static_cast<std::size_t>(i)];
    const std::uint8_t land = c.land;
    const std::uint8_t lvl = c.level;
    const std::uint16_t zone = c.zone;
    f.write(reinterpret_cast<const char*>(&land), 1);
    f.write(reinterpret_cast<const char*>(&lvl), 1);
    f.write(reinterpret_cast<const char*>(&zone), 2);
  }
}

// 按原版 work 记录（0x50 字节）的字段偏移打包导出，便于与实机 work_0x50.bin
// 做字段级对比。只写模型里有对应语义的字段，其余字节留 0（对比时须跳过）。
void WriteWorkDump(const std::string& path,
                   const std::vector<ra2::rmg::PaintWorkCell>& work) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  std::vector<char> rec(0x50, 0);
  for (const auto& c : work) {
    std::fill(rec.begin(), rec.end(), static_cast<char>(0));
    const std::int32_t coord =
        (static_cast<std::int32_t>(c.coord.y) << 16) |
        (static_cast<std::int32_t>(c.coord.x) & 0xFFFF);
    const std::int32_t owner = c.blob_id;
    const std::int32_t queued = c.queued;
    std::memcpy(rec.data() + 0x00, &coord, 4);
    std::memcpy(rec.data() + 0x08, &c.height, 8);
    std::memcpy(rec.data() + 0x10, &c.slope, 8);
    std::memcpy(rec.data() + 0x18, &c.prob_rough, 8);
    std::memcpy(rec.data() + 0x20, &c.prob_green, 8);
    std::memcpy(rec.data() + 0x28, &c.prob_sand, 8);
    std::memcpy(rec.data() + 0x38, &owner, 4);
    std::memcpy(rec.data() + 0x3C, &queued, 4);
    const std::int32_t mcache = c.mask_cache;
    std::memcpy(rec.data() + 0x40, &mcache, 4);
    rec[0x44] = c.flag44 ? 1 : 0;
    rec[0x45] = c.flag45 ? 1 : 0;
    rec[0x47] = c.flag47 ? 1 : 0;
    rec[0x4A] = c.flag4a ? 1 : 0;
    rec[0x4B] = c.flag4b ? 1 : 0;
    f.write(rec.data(), static_cast<std::streamsize>(rec.size()));
  }
}

void WriteArlStateDump(const std::string& path,
                       const ra2::cli::ArlStateSnapshot& s,
                       const std::vector<char>& exists) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return;
  }
  f << "RNG " << s.rng_calls << " " << s.rng_index1 << " " << s.rng_index2;
  for (std::uint32_t v : s.rng_state) f << " " << v;
  f << "\n";
  f << "ND " << (s.normal_cached ? 1 : 0) << " "
    << std::setprecision(17) << s.normal_value << "\n";
  const ra2::rmg::TheaterData& t = *s.theater_data;
  f << "GLOB side=" << s.side << " map_w=" << s.map_w << " map_h="
    << s.map_h << " region_size=" << s.region_size << " map_type="
    << s.map_type << " accessibility=" << s.accessibility << " theater="
    << s.theater << " water_set=" << t.water_set << " shore="
    << t.shore_pieces << " green=" << t.green_tile << " clear_lat="
    << t.clear_to_green_lat << " wf_e=" << t.waterfall_east << " wf_w="
    << t.waterfall_west << " wf_n=" << t.waterfall_north << " wf_s="
    << t.waterfall_south << " cliff_set=" << t.cliff_set
    << " cliff_ramps=" << t.cliff_ramps << " water_caves="
    << t.water_caves << " bridge_set=" << t.bridge_set
    << " wood_bridge_set=" << t.wood_bridge_set << " destroyable_cliffs="
    << t.destroyable_cliffs << " water_cliffs=" << t.water_cliffs
    << " slope_set_pieces=" << t.slope_set_pieces << "\n";
  for (int y = 0; y < s.side; ++y) {
    for (int x = 0; x < s.side; ++x) {
      const ra2::rmg::PaintWorkCell& w =
          s.work[static_cast<std::size_t>(y * s.side + x)];
      f << "W " << x << " " << y << " " << w.coord.x << " " << w.coord.y
        << " " << w.blob_id << " " << w.queued << " " << (w.flag4b ? 1 : 0)
        << " " << (w.flag44 ? 1 : 0) << " " << (w.flag45 ? 1 : 0) << " "
        << (w.flag47 ? 1 : 0) << " " << (w.flag4a ? 1 : 0) << "\n";
    }
  }
  for (int slot = 0; slot < 0x40000; ++slot) {
    if (exists[static_cast<std::size_t>(slot)] == 0) continue;
    const int x = slot & 0x1FF;
    const int y = slot >> 9;
    const ra2::cli::CliCell& c = s.cells[static_cast<std::size_t>(slot)];
    f << "C " << x << " " << y << " "
      << static_cast<std::uint16_t>(c.isoTile) << " "
      << static_cast<int>(c.subtile) << " " << static_cast<int>(c.level)
      << " " << static_cast<int>(c.ramp) << " " << c.overlay << " "
      << static_cast<int>(c.overlayData) << " "
      << static_cast<int>(c.zoneId) << "\n";
  }
}

std::string ArgValue(int argc, char** argv, const char* key) {
  const std::size_t n = std::strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], key, n) == 0 && argv[i][n] == '=') {
      return argv[i] + n + 1;
    }
  }
  return std::string();
}

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

}  // namespace

// SEH 兜底：无头跑流水线崩了能打印崩溃地址（调试用；Release 也能用）。
static int RunCli(int argc, char** argv) {
  const std::string sed_path = ArgValue(argc, argv, "--sed");
  const std::string game_dir = ArgValue(argc, argv, "--dir");
  const std::string out_path = ArgValue(argc, argv, "--out");
  const std::string seed_tok = ArgValue(argc, argv, "--seed");
  const std::string rules_path = ArgValue(argc, argv, "--rules");
  const std::string dump_path = ArgValue(argc, argv, "--dump-cells");
  const std::string dump_rng_path = ArgValue(argc, argv, "--dump-rng");
  const std::string dump_stage_rng_path = ArgValue(argc, argv, "--dump-stage-rng");
  const std::string dump_ore_trace_path = ArgValue(argc, argv, "--dump-ore-trace");
  const std::string dump_tech_trace_path = ArgValue(argc, argv, "--dump-tech-trace");
  const std::string dump_start_trace_path = ArgValue(argc, argv, "--dump-start-trace");
  const std::string dump_arl_state_path = ArgValue(argc, argv, "--dump-arl-state");
  const std::string dump_stage_cells_prefix =
      ArgValue(argc, argv, "--dump-stage-cells-prefix");
  const std::string dump_stage_ground_prefix =
      ArgValue(argc, argv, "--dump-stage-ground-prefix");
  const std::string dump_ground_path = ArgValue(argc, argv, "--dump-ground");
  const std::string dump_stage_work_prefix =
      ArgValue(argc, argv, "--dump-stage-work-prefix");
  const std::string dump_stage_work_names =
      ArgValue(argc, argv, "--dump-stage-work-names");
  const std::string dump_ore_pop_path = ArgValue(argc, argv, "--dump-ore-pop");
  const std::string dump_ore_work_prefix =
      ArgValue(argc, argv, "--dump-ore-work");
  const std::string dump_ore_enq_path = ArgValue(argc, argv, "--dump-ore-enq");
  const std::string dump_cells_at_rng = ArgValue(argc, argv, "--dump-cells-at-rng");
  const std::string dump_cells_at_rng_out = ArgValue(argc, argv, "--dump-cells-at-rng-out");
  const std::string dump_cells_before_rng =
      ArgValue(argc, argv, "--dump-cells-before-rng");
  const std::string dump_cells_before_rng_out =
      ArgValue(argc, argv, "--dump-cells-before-rng-out");
  const std::string dump_zone_table_path =
      ArgValue(argc, argv, "--dump-zone-table");

  if (sed_path.empty() || game_dir.empty()) {
    std::fprintf(stderr,
                 "usage: ra2_rmg_cli --sed=RandMap.Sed --dir=GAME_DIR "
                 "[--out=out.map] [--seed=NNN] [--rules=rulesmd.ini]\n");
    return 2;
  }

  // ── 1. 读 .sed ──
  ra2::rmg::MapSeedParams params;
  if (!ra2::rmg::LoadSedFile(sed_path, &params)) {
    std::fprintf(stderr, "cannot load sed: %s\n", sed_path.c_str());
    return 1;
  }

  // ── 2. 种子 ──
  long long seed = params.seed;
  if (!seed_tok.empty()) {
    seed = std::strtoll(seed_tok.c_str(), nullptr, 0);
  }
  if (seed < 0 || seed > 0xFFFFFFFFLL) {
    std::fprintf(stderr,
                 "seed must be in [0, 2^32) (.sed Seed=%d, --seed=%s)\n",
                 params.seed, seed_tok.c_str());
    return 1;
  }

  // ── 3. 装载 theater ──
  ra2::assets::TheaterAssets assets;
  ra2::assets::TheaterAssets::Theater th =
      (params.theater == 1) ? ra2::assets::TheaterAssets::kSnow
                            : ra2::assets::TheaterAssets::kTemperate;
  std::string err;
  if (!assets.Load(game_dir, th, &err)) {
    std::fprintf(stderr, "theater load failed: %s\n", err.c_str());
    return 1;
  }
  std::vector<ra2::rmg::TileType> tiles;
  assets.BuildRmgTileTable(&tiles);

  // ── 4. rulesmd.ini + rmgmd.ini ──
  ra2::cli::RmgRules rules;
  std::string rules_text;
  if (!rules_path.empty() && ReadFile(rules_path, &rules_text)) {
    // 指定路径
  } else {
    const std::string loose = game_dir + "\\rulesmd.ini";
    ReadFile(loose, &rules_text);
  }

  std::string rmg_text;
  const std::string loose_rmg = game_dir + "\\rmgmd.ini";
  ReadFile(loose_rmg, &rmg_text);

  if (rules_text.empty() || rmg_text.empty()) {
    std::ifstream mixf(game_dir + "\\ra2md.mix", std::ios::binary);
    if (mixf) {
      std::ostringstream ss;
      ss << mixf.rdbuf();
      std::string mix_data = ss.str();
      ra2::mix::MixArchive mix;
      if (mix.Open(std::vector<std::uint8_t>(mix_data.begin(),
                                             mix_data.end()))) {
        std::vector<std::uint8_t> raw;
        ra2::mix::MixArchive local;
        const bool has_local = local.OpenNested(mix, "localmd.mix");
        if (rules_text.empty()) {
          if (mix.Read("rulesmd.ini", &raw)) {
            rules_text.assign(raw.begin(), raw.end());
          } else if (has_local && local.Read("rulesmd.ini", &raw)) {
            rules_text.assign(raw.begin(), raw.end());
          }
        }
        if (rmg_text.empty() && has_local && local.Read("rmgmd.ini", &raw)) {
          rmg_text.assign(raw.begin(), raw.end());
        }
      }
    }
  }
  if (rules_text.empty()) {
    std::fprintf(stderr, "cannot find rulesmd.ini (pass --rules)\n");
    return 1;
  }
  if (!rmg_text.empty()) {
    rules_text.push_back('\n');
    rules_text.append(rmg_text);
    std::fprintf(stderr, "[DEBUG] Loaded rmgmd.ini (%zu bytes)\n", rmg_text.size());
  } else {
    std::fprintf(stderr, "[DEBUG] WARNING: rmgmd.ini not found, using defaults\n");
  }
  ra2::cli::LoadRmgRules(rules_text, &rules);
  std::fprintf(stderr, "[DEBUG] Loaded rules: MaxTrees=%d MinTiberium=%d MaxTiberium=%d\n",
               rules.max_trees, rules.min_tiberium, rules.max_tiberium);

  // Add default neutral tech buildings if not specified in rules
  if (rules.neutral_tech_buildings.empty()) {
    rules.neutral_tech_buildings = {
      "CAARMR", "CAAIRP", "CAPOWR", "CARADR", "CAMACH", "CATECH", "CAWEAP",
      "NAPOWR", "NARADR", "NATECH", "NAWEAP",
      "CAOILD", "CAOUTP", "CATHOSP", "CAMORY",
      "GACNST", "NACNST", "YACNST"
    };
    std::fprintf(stderr, "[DEBUG] Using default NeutralTechBuildings (%zu types)\n",
                 rules.neutral_tech_buildings.size());
  } else {
    std::fprintf(stderr, "[DEBUG] Loaded NeutralTechBuildings: %zu types\n",
                 rules.neutral_tech_buildings.size());
  }

  rules.foundations = assets.foundations();

  // ── 5. 流水线 ──
  ra2::cli::PipelineResult result;
  result.capture_stage_cells = !dump_stage_cells_prefix.empty() ||
                               !dump_stage_ground_prefix.empty() ||
                               !dump_stage_work_prefix.empty();
  result.capture_stage_work = !dump_stage_work_prefix.empty();
  if (!dump_stage_work_names.empty()) {
    std::stringstream ns(dump_stage_work_names);
    std::string item;
    while (std::getline(ns, item, ',')) {
      if (!item.empty()) result.stage_work_names.push_back(item);
    }
  }
  result.capture_arl_state = !dump_arl_state_path.empty();
  result.ore_work_dump_prefix = dump_ore_work_prefix;
  result.capture_stage_rng_state = !ArgValue(argc, argv, "--dump-rng-at-after-hills").empty();
  if (!dump_cells_at_rng.empty()) {
    result.capture_rng_call_cells = std::strtoll(dump_cells_at_rng.c_str(), nullptr, 0);
  }
  if (!dump_cells_before_rng.empty()) {
    result.capture_before_rng_call_cells =
        std::strtoll(dump_cells_before_rng.c_str(), nullptr, 0);
  }
  if (!dump_zone_table_path.empty()) {
    result.capture_stage_cells = true;   // zone 表拷贝挂在这个开关下
  }
  if (!ra2::cli::RunPipeline(params, static_cast<unsigned>(seed),
                             assets.data(), tiles, rules, assets.TileCount(),
                             &result, &err)) {
    std::fprintf(stderr, "pipeline failed: %s\n", err.c_str());
    return 1;
  }

  // ── 6. 写 .map ──
  const std::string map_text =
      ra2::cli::BuildMapFile(result, params.theater, 4);
  const std::string out = out_path.empty() ? "rmcache\\RandMap.Map" : out_path;
  std::ofstream f(out, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", out.c_str());
    return 1;
  }
  f << map_text;

  // ── 6.5 导出 cells 表（e2e 对拍）──
  // 每格 9 字节：exists(1) isoTile(2) subtile(1) level(1) ramp(1)
  //             overlayData(1) overlay(2)，索引 (Y<<9)+X，共 0x40000 格。
  // 游戏侧同字段：CellClass +0x38/+0x44/+0x11A/+0x11B/+0x11C/+0x11E。
  if (!dump_path.empty()) {
    WriteCellsDump(dump_path, result.cells, result.exists);
  }

  if (!dump_cells_at_rng_out.empty()) {
    if (!result.rng_call_cells_valid) {
      std::fprintf(stderr, "RNG cell capture did not hit call %s\n",
                   dump_cells_at_rng.c_str());
      return 1;
    }
    WriteCellsDump(dump_cells_at_rng_out, result.rng_call_cells, result.exists);
  }

  if (!dump_cells_before_rng_out.empty()) {
    if (!result.before_rng_call_cells_valid) {
      std::fprintf(stderr, "RNG before-cell capture did not hit call %s\n",
                   dump_cells_before_rng.c_str());
      return 1;
    }
    WriteCellsDump(dump_cells_before_rng_out, result.before_rng_call_cells,
                   result.exists);
  }

  if (!dump_rng_path.empty()) {
    std::ofstream rf(dump_rng_path, std::ios::binary);
    if (!rf) {
      std::fprintf(stderr, "cannot write %s\n", dump_rng_path.c_str());
      return 1;
    }
    const std::uint8_t disabled = 0;
    rf.write(reinterpret_cast<const char*>(&disabled), 1);
    const std::uint8_t pad[3] = {0, 0, 0};
    rf.write(reinterpret_cast<const char*>(pad), 3);
    const std::int32_t i1 = result.rng_index1;
    const std::int32_t i2 = result.rng_index2;
    rf.write(reinterpret_cast<const char*>(&i1), 4);
    rf.write(reinterpret_cast<const char*>(&i2), 4);
    for (std::uint32_t v : result.rng_state) {
      rf.write(reinterpret_cast<const char*>(&v), 4);
    }
  }

  if (!dump_stage_rng_path.empty()) {
    std::ofstream sf(dump_stage_rng_path, std::ios::binary);
    if (!sf) {
      std::fprintf(stderr, "cannot write %s\n", dump_stage_rng_path.c_str());
      return 1;
    }
    for (const auto& cp : result.rng_checkpoints) {
      sf << cp.name << " calls=" << cp.calls << " index1=" << cp.index1
         << " index2=" << cp.index2 << " normal_cached="
         << (cp.normal_cached ? 1 : 0) << " normal_value="
         << cp.normal_value;
      if (cp.has_state) {
        sf << " state=";
        for (std::uint32_t v : cp.state) {
          sf << std::hex << v << ",";
        }
        sf << std::dec;
      }
      sf << "\n";
    }
  }

  if (!dump_ore_trace_path.empty()) {
    std::ofstream of(dump_ore_trace_path, std::ios::binary);
    if (!of) {
      std::fprintf(stderr, "cannot write %s\n", dump_ore_trace_path.c_str());
      return 1;
    }
    of << "kind region index wp_base start_points candidates cells total avg special "
       << "field_id size normal seed_x seed_y gems before after delta "
       << "pop_count restarts pool_overflow\n";
    for (const auto& tr : result.ore_trace) {
      of << tr.kind << " " << tr.region << " " << tr.index << " "
         << tr.wp_base << " " << tr.start_points << " " << tr.candidates
         << " " << tr.cells << " " << tr.total << " " << tr.avg << " "
         << tr.special << " " << tr.field_id << " " << tr.size << " "
         << std::setprecision(17) << tr.normal << " " << tr.seed_x << " "
         << tr.seed_y << " " << tr.gems << " " << tr.before << " "
         << tr.after << " " << (tr.after - tr.before) << " "
         << tr.pop_count << " " << tr.restarts_used << " " << tr.pool_overflow << "\n";
    }
  }

  if (!dump_ore_pop_path.empty()) {
    std::ofstream of(dump_ore_pop_path, std::ios::binary);
    if (!of) {
      std::fprintf(stderr, "cannot write %s\n", dump_ore_pop_path.c_str());
      return 1;
    }
    of << "field_id x y od_before od_after\n";
    for (const auto& e : result.ore_pop_trace) {
      of << e.field_id << " " << e.x << " " << e.y << " "
         << e.od_before << " " << e.od_after << "\n";
    }
  }

  if (!dump_ore_enq_path.empty()) {
    std::ofstream of(dump_ore_enq_path, std::ios::binary);
    if (!of) {
      std::fprintf(stderr, "cannot write %s\n", dump_ore_enq_path.c_str());
      return 1;
    }
    of << "field_id x y marker rng_rv\n";
    for (const auto& e : result.ore_enq_trace) {
      of << e.field_id << " " << e.x << " " << e.y << " "
         << e.od_before << " " << e.od_after << "\n";
    }
  }

  if (!dump_tech_trace_path.empty()) {
    std::ofstream tf(dump_tech_trace_path, std::ios::binary);
    if (!tf) {
      std::fprintf(stderr, "cannot write %s\n", dump_tech_trace_path.c_str());
      return 1;
    }
    tf << "type type_index attempt x y ok unlimbo\n";
    for (const auto& tr : result.tech_trace) {
      tf << tr.type << " " << tr.type_index << " " << tr.attempt << " "
         << tr.x << " " << tr.y << " " << (tr.ok ? 1 : 0) << " "
         << (tr.unlimbo ? 1 : 0) << "\n";
    }
  }

  if (!dump_start_trace_path.empty()) {
    std::ofstream sf(dump_start_trace_path, std::ios::binary);
    if (!sf) {
      std::fprintf(stderr, "cannot write %s\n", dump_start_trace_path.c_str());
      return 1;
    }
    sf << "kind region base start_points cells candidates extra budget tries key x y "
          "movement_zone want_zone_type largest_zone zone_count before after\n";
    for (const auto& tr : result.start_trace) {
      sf << tr.kind << " " << tr.region << " " << tr.base << " "
         << tr.start_points << " " << tr.cells << " " << tr.candidates << " "
         << tr.extra << " " << tr.budget << " " << tr.tries << " "
         << tr.key << " " << tr.x << " " << tr.y << " "
         << tr.movement_zone << " " << tr.want_zone_type << " "
         << tr.largest_zone << " " << tr.zone_count << " "
         << tr.before << " " << tr.after << "\n";
    }
  }

  if (!dump_stage_cells_prefix.empty()) {
    for (const auto& s : result.cell_snapshots) {
      WriteCellsDump(dump_stage_cells_prefix + "_" + s.name + ".bin", s.cells,
                     result.exists);
    }
  }

  if (!dump_stage_ground_prefix.empty()) {
    for (const auto& s : result.cell_snapshots) {
      WriteGroundDump(dump_stage_ground_prefix + "_" + s.name + ".bin",
                      s.cells, result.exists);
    }
  }

  if (!dump_stage_work_prefix.empty()) {
    for (const auto& s : result.cell_snapshots) {
      if (!s.has_work) continue;
      WriteWorkDump(dump_stage_work_prefix + "_" + s.name + ".bin", s.work);
    }
  }

  if (!dump_arl_state_path.empty()) {
    if (!result.arl_state.valid) {
      std::fprintf(stderr, "ARL state was not captured\n");
      return 1;
    }
    WriteArlStateDump(dump_arl_state_path, result.arl_state, result.exists);
  }

  if (!dump_zone_table_path.empty()) {
    if (result.zone_table_beforerebuild.empty()) {
      std::fprintf(stderr, "zone table was not captured\n");
      return 1;
    }
    WriteZoneTableDump(dump_zone_table_path, result.zone_table_side,
                       result.zone_table_beforerebuild);
  }

  std::fprintf(stderr,
               "ok: seed=%u players=%d map=%dx%d terrains=%zu buildings=%zu "
               "waypoints=%zu -> %s\n",
               static_cast<unsigned>(seed), params.num_players,
               result.map_w, result.map_h, result.terrains.size(),
               result.buildings.size(), result.waypoints.size(), out.c_str());
  return 0;
}

static EXCEPTION_POINTERS* g_exc = nullptr;

int main(int argc, char** argv) {
  __try {
    return RunCli(argc, argv);
  } __except (g_exc = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
    const EXCEPTION_RECORD* rec = g_exc->ExceptionRecord;
    const CONTEXT* ctx = g_exc->ContextRecord;
    std::fprintf(stderr, "CRASH: code=0x%08X at 0x%p fastfail=%u\n",
                 static_cast<unsigned>(rec->ExceptionCode),
                 rec->ExceptionAddress,
                 static_cast<unsigned>(rec->ExceptionInformation[0]));
#if defined(_M_X64)
    std::fprintf(stderr,
                 "  rsp=0x%llX rbp=0x%llX rax=0x%llX rcx=0x%llX rdx=0x%llX "
                 "r8=0x%llX r9=0x%llX rdi=0x%llX\n",
                 static_cast<unsigned long long>(ctx->Rsp),
                 static_cast<unsigned long long>(ctx->Rbp),
                 static_cast<unsigned long long>(ctx->Rax),
                 static_cast<unsigned long long>(ctx->Rcx),
                 static_cast<unsigned long long>(ctx->Rdx),
                 static_cast<unsigned long long>(ctx->R8),
                 static_cast<unsigned long long>(ctx->R9),
                 static_cast<unsigned long long>(ctx->Rdi));
    const volatile unsigned char* sp =
        reinterpret_cast<const volatile unsigned char*>(ctx->Rsp);
    for (unsigned long long off = 0x38; off <= 0x50; off += 8) {
      unsigned long long v = 0;
      memcpy(&v, const_cast<const unsigned char*>(sp + off), sizeof(v));
      std::fprintf(stderr, "  [rsp+0x%02llX]=0x%016llX%s\n", off, v,
                   off == 0x40 ? "  <- w(RegionWorld*)" : "");
    }
#else
    std::fprintf(stderr,
                 "  esp=0x%08X ebp=0x%08X eax=0x%08X ecx=0x%08X edx=0x%08X "
                 "edi=0x%08X esi=0x%08X\n",
                 static_cast<unsigned>(ctx->Esp),
                 static_cast<unsigned>(ctx->Ebp),
                 static_cast<unsigned>(ctx->Eax),
                 static_cast<unsigned>(ctx->Ecx),
                 static_cast<unsigned>(ctx->Edx),
                 static_cast<unsigned>(ctx->Edi),
                 static_cast<unsigned>(ctx->Esi));
#endif
    return 1;
  }
}
