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
//   --dump-cells  导出 cells 表（e2e 对拍用：0x40000 × 9B，见 cli_main.cpp 注释）
#include <cstdio>
#include <cstring>
#include <fstream>
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

  // ── 4. rulesmd.ini ──
  ra2::cli::RmgRules rules;
  std::string rules_text;
  if (!rules_path.empty() && ReadFile(rules_path, &rules_text)) {
    // 指定路径
  } else {
    const std::string loose = game_dir + "\\rulesmd.ini";
    if (!ReadFile(loose, &rules_text)) {
      // 散装没有，从 ra2md.mix 读
      std::ifstream mixf(game_dir + "\\ra2md.mix", std::ios::binary);
      if (mixf) {
        std::ostringstream ss;
        ss << mixf.rdbuf();
        std::string mix_data = ss.str();
        ra2::mix::MixArchive mix;
        if (mix.Open(std::vector<std::uint8_t>(mix_data.begin(),
                                               mix_data.end()))) {
          std::vector<std::uint8_t> raw;
          if (mix.Read("rulesmd.ini", &raw)) {
            rules_text.assign(raw.begin(), raw.end());
          }
        }
      }
    }
  }
  if (rules_text.empty()) {
    std::fprintf(stderr, "cannot find rulesmd.ini (pass --rules)\n");
    return 1;
  }
  ra2::cli::LoadRmgRules(rules_text, &rules);

  // ── 5. 流水线 ──
  ra2::cli::PipelineResult result;
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
    std::ofstream df(dump_path, std::ios::binary);
    if (!df) {
      std::fprintf(stderr, "cannot write %s\n", dump_path.c_str());
      return 1;
    }
    for (int i = 0; i < 0x40000; ++i) {
      const ra2::cli::CliCell& c = result.cells[static_cast<std::size_t>(i)];
      const std::uint8_t exists = (result.exists[static_cast<std::size_t>(i)] != 0);
      const std::uint8_t st = static_cast<std::uint8_t>(c.subtile);
      const std::uint8_t lv = static_cast<std::uint8_t>(c.level);
      const std::uint8_t rm = static_cast<std::uint8_t>(c.ramp);
      const std::uint8_t od = static_cast<std::uint8_t>(c.overlayData);
      const std::uint16_t ov = static_cast<std::uint16_t>(c.overlay);
      const std::uint16_t iso = static_cast<std::uint16_t>(c.isoTile);
      df.write(reinterpret_cast<const char*>(&exists), 1);
      df.write(reinterpret_cast<const char*>(&iso), 2);
      df.write(reinterpret_cast<const char*>(&st), 1);
      df.write(reinterpret_cast<const char*>(&lv), 1);
      df.write(reinterpret_cast<const char*>(&rm), 1);
      df.write(reinterpret_cast<const char*>(&od), 1);
      df.write(reinterpret_cast<const char*>(&ov), 2);
    }
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
    // 崩溃指令是 mov rax,[rax]；[rsp+0x40] 为 home 区里的 w（RegionWorld*），
    // [rsp+0x38] 为返回地址（序言 push rdi + sub 0x30）。
    const volatile unsigned char* sp =
        reinterpret_cast<const volatile unsigned char*>(ctx->Rsp);
    for (unsigned long long off = 0x38; off <= 0x50; off += 8) {
      unsigned long long v = 0;
      memcpy(&v, const_cast<const unsigned char*>(sp + off), sizeof(v));
      std::fprintf(stderr, "  [rsp+0x%02llX]=0x%016llX%s\n", off, v,
                   off == 0x40 ? "  <- w(RegionWorld*)" : "");
    }
    return 1;
  }
}
