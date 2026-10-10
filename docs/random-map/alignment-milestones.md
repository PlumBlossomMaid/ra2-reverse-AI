# RMG 对齐里程碑结论（2026-10-06/07，Seed=62298 主线）

> 来源：原 AGENTS.md（kimi-k3）。本文汇总"已闭合"的机制级结论；开放的 mt0/mt1 发散见
> mt0-mt1-open-issues.md，穷举口径见 mega-sweep.md，采集方法见 live-capture-toolchain.md。

## 权威基准

`E:\code\ra2-reverse-AI\build_ninja62298\save_runtime_cells_62298.bin`
（SHA=81494d1f…）是**唯一成品权威**。
`snapshot_gamemd_62298\game_cells.bin` 是活体对局中抓的、被矿生长污染，勿当基准。
`build_ninja62298\actual_randmap_62298.map`（2026-08-30）与权威**不是同一局**
（Waypoints 与 TIBTRE 位置全不同），勿当 62298 地面真值。

## od 平滑（矿格外框）——纯静态攻破

- rm​g_done 窗口（0x599354-0x5994B0）内唯一变化 = 矿格 od（iso/sub/lv/ramp/ov 不变）。
- 映射是确定性函数：`od_new = T[pc]`，pc = 8 邻域（原版 0x89F688 表）内**同类矿**
  （ore 27-38 / gem 102-113 各算各的）overlay 数；
  T = {0:0,1:1,2:3,3:4,4:6,5:7,6:8,7:10,8:11} = `(pc*11+4)>>3`。316/316 零例外。
- **原版机器码全链**：调用点 = `0x59944C call 0x568BB0(MapClass, 1)`；
  写点 = `0x4819D8 mov byte ptr [esi+0x11e], al`（函数 0x4818E0，0x568BB0 逐格循环
  在 0x568DD8 对矿格调用）；机制 = 数 8 邻域同 TiberiumIndex 格数 →
  `idiv [type+0xE4]` 取余 → od = **dword 表 [0x81CD28+余数*4] 低字节**。
  表前 9 项 {0,1,3,4,6,7,8,10,11} 与 (pc*11+4)>>3 完全一致。
  早期扫描未命中的原因：按连续**字节**搜表，实际表存 dword。
- CLI 已改为字面表查表（pipeline.cpp after_ore_frame_smooth）。

## rocks + 树林分岔——CreateTerrain 占格修复

- 根因：`CliWorld::CreateTerrain` 只记录 terrains_ 列表，**从不置 cell.object
  (+0xE4)** → GrowTreePatch 种植检查 `Field0xE4==0` 永真 → 同一格重复种树。
  原版 CreateTerrain=0x71BB90 会置 +0xE4。
- 修复：`Mut(at).object = 1;` 一行。修复后树林 40/40 draws 与 emu 完全一致，
  rocks 18/18 全中 live rmg_done。

## +0xEC(ground)/+0x4C(zoneId) 两列机制

1. **0x47D318：overlay != -1 → +0xEC = OverlayTypeClass+0x298 无条件写**。
   +0x298∈{4,9} 或 +0x2AC≠0 → overlay 专属路径 0x47D333。+0x2AC 的 INI 口径 =
   `Tiberium=yes || NoUseTileLandType=true`。**矿石区间 27-38/102-113 恒
   {land=5, 占地}，优先于 INI 表**。
2. **overlay 路径直达 RecalcLandType（0x47D551）**——ground==5 早退跳过是错的。
3. **0x483C80 全分支**（RecalcLandTypeFull）：出图→7；overlay +0x22D→1/+0x2A8→2/
   Ground[+0x298].Foot==0.0f→6/+0x2B5→6/+0x2B4→0；+0xEC==2→4/==6→3/Foot≤0.01→6；
   +0xE4 占据者循环：**RTTI 0x24 树 → TemperateOccupationBits==7（雪地查 Snow）→
   zone 2，否则 5**；其他 RTTI → 0。

## .map 键编码与坐标语义

- 键 = **y*1000+x**（y 在千位）。实锤：0x68BE0C V%1000→低 16 位、V/1000→高 16 位；
  GetCellAt(0x5657A0) 低=X 高=Y。2026-08-15 的「x*1000+y 实锤」是与自建脚本的
  循环论证，作废。
- Waypoints/Terrain/Structures 的 x/y 是**等距坐标**（cell 列/行，范围 [0, W+H]），
  不是真实坐标（官方图 sandwell Terrain (93,179)、捡箱子 Waypoint (202,364) 超
  地图尺寸即证据）。CLI 等距直写。Structures 字段序 owner,type,health,**y,x**,dir。
- 游戏不写 rmcache\RandMap.Map——存档 CONTENTS 只引用 RANDMAP.SED（种子），重载按
  当前种子重建（进遭遇战会覆盖种子→存档图变）。GUI:SaveMapMenu 是 DebugLog 桩。

## 内部状态全量对拍（live 同会话）

- cells 全字段 6 点（tiberium_entry→rmg_done）：0 差异；RNG 位置与全状态（state[250]）
  逐字节 0 差异。原版 Randomizer @0xABE890 布局：+0=虚表指针，+4=index1、+8=index2、
  +0xC 起 state[250]，共 0x3F4。
- work 数组仅 8 格 mask_cache(+0x40) 错位，已闭合：原版 ShoreMask (0x57B210) 直接查
  格指针表，指针为空的邻格用 self_is_water 顶替、**不读哑元**；CLI 误走
  CliWorld::IsoTile 的哑元回退。

## emu 对拍的两个基准错误（已修正，勿复现旧结论）

1. Oracle 尺寸必须 `OreOracle(74, 82)`（side=157），不是 (70,70)；
   MAP+0xFC/+0x100/+0x104/+0x108 = 2/5/70/70（local 矩形）。
2. 两片矿田间外层 NormalDeviate 消耗主 RNG，emu 重放须按 CLI ore_trace 的
   before/after 间隙补抽 `0x65C780`（gap 实测 2/0/4/0/4/0/4/0/0）。

## 已排除的根因（别再查）

SpreadKey/FastSqrt 精度；ContainedTiberiumIndex 区间；NormalDeviate 入口态；
「Push 失败 used++」（反汇编 0x5A2DDC 证伪）；哑元 0xABDC50（全程零命中）；
work.queued/flag45 入口态（全表 0 diff）。

## 造山阶段（CreateHills）emu 灌入坑（_emu_hills.py）

1. `[0x87F8DC]` = **MapRect.W = W**（MapSeed+0x180 插值），**不是 side**！
   CellIterator(0x578350) 起点 = (1, W)。误灌 side=157 → 漏一半 cell。
2. `[0xABC1D8]` = ramp_base（温带 29）：k≠0 分支写 tile = ramp_base+k-1；
   不灌则 = 0，Apply 写 tile 全错。
3. **CellIterator 终止机制**：Next(0x578290) 返回槽指针，槽为 NULL 时循环退出——
   遍历到地图菱形外第一个空槽即停，覆盖 X+Y ∈ [W+1, 2W+2H-2]。

## MakingRegions（阶段 4）

- RepaintAll 0x578E60 owner 参数 = **-1**（调用点 0x598D73 push -1）。
- SplitRegion 防重：0x89A304 由 `MapClass::Set_Dimensions` 按 side 写入 8 邻域 work
  索引步长；扩散里 0x58DA5B 标记候选格 + 0x58D9EA 命中跳过 → **防重有效、命中不耗
  RNG**。golden 生成器须灌 0x89A304，否则是"不防重"假象。
- syn-rs99 两个深层分裂用例的 Rebuild 段 14 抽残差按 `known_residual` 显式登记，
  不阻塞 CI；定位线索 = cell (14,9) 归属错位 → SeedRegion DFS 前 blob_id 状态 3 格差
  （0x58C9AC 反编译 `== local_3c` 与逐字节 `cmp [..],-1` 矛盾，试装后真实地形用例
  崩、已回退）。

## 规则装载

- art 系 INI 的 `Foundation=` → 0x89C900 静态占地表（stride 120B，全是 row-major
  矩形，仅 3x3Refinery 缺 (2,1)）。CLI 装载顺序：art.ini（ra2.mix→local.mix）→
  artmd.ini（ra2md.mix→localmd.mix）→ expandmd01 artmd.ini，后者盖前者。
- theater1 修复：morphable 逐砖表改由 theater ini 的 Morphable= 键展开（commit b8d3e84）。
- SmoothShoreline 按原版双调用点分 flag——涂块收尾 0x59C433=1，湖/瀑布收尾
  0x59D282=0（commit 4ec579d）。

---

## 🎉 8960 案全量逐字节对齐达成（2026-10-10）

**结论**：种子 12345 与 62298 各 4480 案（mt5×t4×th2×sz4×r4×p8），
CLI 最终态与游戏侧 cleanregen 真值 **pass=4480 diff=0 missing=0**
（两 seed 全 8960 案逐字节一致，`sweeps_cmp_<seed>.txt`）。

**最后一公里**：seed=12345 修复前剩 632 案（sz1/sz2）的 isoTile 发散。
根因 = `pipeline.cpp` 的 `world.SetDummyIsoTile(theater.green_tile)`
把共享哑元 0xABDC50 钉死成 493，导致 `SetupLAT` 段 3 ClearToGreenLat
把出图邻居误判为「同组」（m 少置位 → 少产过渡砖 503）。
游戏真值 dump 实锤：出图格 (57,30)/(56,31) 的 iso=**0**（不是 493），
游戏靠哑元=0 置出过渡位（m=9 → 503）。移除钉死后哑元保持 0，与游戏一致。

**哑元残留铁律**：出图格（exists=0）读 iso 回退共享哑元，其值在
LAT 段 3 时刻为 **0**（Seeding water (h) 段沿岸刷绿会把哑元写成
green_tile，但后续阶段又改回 0）。不得钉死——钉死会让
ClearToGreenLat 的豁免集合误命中（{green}∪[clear_to_green_lat,+0xF]∪
[shore,+0x29]∪[water_bridge,+1] 不含 0）。

**管道死锁坑**：`_cli_sweep.py` 的 `subprocess.run(capture_output=True)`
在 Windows 上会管道死锁（CLI stderr 输出超过管道缓冲，CLI 阻塞写 stderr，
communicate 读不到）。修复：重定向 stdout/stderr 到文件
（`stdout=lf, stderr=subprocess.STDOUT`）。
