# .map 输出格式（CLI 写链）

`code/rewrite/cli/mapwriter.{h,cpp}` 组装 RandMap.Map。本文记录已实锤的格式
与仍待 e2e 对拍裁决的部分。

## 2026-10-06 结构对拍结果（CLI vs 真实游戏捕获 vs 官方图）

基准：`snapshot_gamemd_62298/RandMap_62298_after_gate2.Map`（2026-08-15
真实游戏写出的 RMG 图，70×70 温和，注意该局参数与当前 .sed 不同，
waypoint (90,22)/(40,105)，只能比结构不能比内容）+ sandwell/捡箱子官方图。
脚本 `.qwen/tmp/_map_struct_cmp.py`、`_map_overlay_oob_check.py`。

**已对齐**（CLI == 游戏捕获，逐项实锤）：

- 节集与节序：`Map Basic IsoMapPack5 OverlayPack OverlayDataPack Waypoints
  Terrain [Structures]`，逐节顺序一致；游戏 RMG 图**没有** Header/Houses/
  Digest/Preview（那些是编辑器产物），CLI 同样不写。
- [Map]：`Size=0,0,74,82 / LocalSize=2,5,70,70 / Theater=TEMPERATE / Level=4`
  四键逐字符相同（含键序）。
- [Basic]：`Name=Random Map` + `MultiplayerOnly=1`，两键相同。
- IsoMapPack5：同为 17 个 LZO 块、解压 132598B、12054 条 11B 记录、
  首/尾边界记录逐字节相同、base64 70 列、键从 1 起编号。
  压缩器字节级等价已由 test_mapenc（Unicorn golden）覆盖。
- OverlayPack/OverlayDataPack：同为 32 段 format-80（段头 (c,u=8192)），
  **菱形外 250090 字节两边完全一致**（OverlayPack=0xFF、OverlayDataPack=0）。
  菱形内差异属不同局内容差异（矿位不同），非格式问题。
- [Waypoints]/[Terrain]：键 `x*1000+y`、值格式一致。

**仍待取证**：

1. **[Structures] 行格式**：游戏捕获那局没有该节（可能 MapType=0 没放
   科技建筑）。CLI 现写 `1=0,CAOUTP,256,95,40,0,0,...,0`（owner 写 "0"、
   tag/upgrade 写 "0"、f1/energy 写 0、键从 1 起）；编辑器写链
   （MapData.cpp:2492 AddStructure）的默认是 owner=**"Neutral"**、dir=**64**、
   tag=**"None"**、f1=**1**、energy=**1**、upgrade1..3=**"None"**、键从 0 起。
   游戏 RMG 写链实际写什么需一次 MapType=3 的活体捕获裁决
   （暂停菜单的 Save Map，用户记得流程）。
2. IsoMapPack5 同会话逐字节对拍（需当前 .sed 参数的新鲜游戏捕获）。

## 当前无游戏条件下已闭合的一项输出链问题（2026-08-15）

`mapwriter.cpp` 原先为 `IsoMapPack5` 的临时记录池固定预留 `0x10000` 个元素，
只要大地图菱形内有效格超过该数量，后续 `push_back` 就会让 `slots` 中保存的
记录指针全部失效，最终压缩出的地图数据属于未定义结果。现已改为先统计
`exists` 数量，再按实际数量一次性 `reserve`；这不改变小地图输出字节，只消除
大地图输入下的潜在首个输出面分岔。`test_mapenc`、`test_cli` 与全量 ctest 已复验。

## 已实锤：坐标语义（2026-08-13 修正）

**Waypoints / Terrain / Structures 的 x/y 全部是"等距坐标"**（cell 列/行，
范围 [0, W+H]），**不是真实坐标**。CLI 直接输出等距坐标，**不做任何换算**
（2026-08-12 版曾误判为真实坐标并加 `ToReal` 换算，导致坐标全部错位——
FA2 只显示 1 个玩家；2026-08-13 依官方图重判并删除换算）。

两条独立证据（本机官方图，FinalAlert2 编辑器制作）：

- `E:\YRLauncher\捡箱子.yrm`（[Map] Size=0,0,200,200 → W+H=400）：
  `[Waypoints] 1=202364` → PosToXY 解析 x=202、y=364。y=364 超出地图高度 200，
  **真实坐标不可能**；等距坐标 364 ∈ [0, 400] 合法。waypoint (374,203)、(202,364)
  等全部落在等距范围。
- `E:\YRLauncher\sandwell.yrm`（[Map] Size=0,0,100,150 → W+H=250）：
  `[Terrain] 93179=TIBTRE02` → (93,179)：y=179 > 150，真实坐标越界；
  等距 179 ∈ [0, 250] 合法。waypoint (229,148) 同理。
- FA2 消费方式实锤：`GetWaypointData`（MapData.cpp:2979）把解析出的
  `x + y * GetIsoSize()` 当 **cell 索引**用（GetIsoSize = MapRect 宽+高，
  与游戏 `(y<<9)+x` 同构）——只有等距坐标才能当 cell 索引。

**2026-08-12 的"真实坐标"结论是错的**，当时的推理把"y 超出地图尺寸"当成
真实坐标的证据，实际那正是等距坐标的证据；`ToReal` 换算已从 mapwriter 删除。

## 已实锤：键/值编码

- **键（Waypoints 的键、Terrain 的键）** = `x * 1000 + y` 的十进制字符串。
  Seed=62298 的真实游戏捕获 `RandMap_62298_after_gate2.Map` 与 CLI 逐字节对拍
  实锤：waypoint `(90,22)` 写作 `0=90022`，第一棵矿树 `(46,56)` 写作
  `46056=TIBTRE01`。此前按 EA 编辑器读链推断成 `y * 1000 + x` 是错误结论。
- **Structures 值** = 17 字段逗号表，顺序（GetParam 0..16，MapData.cpp:1650）：
  `owner,type,health,y,x,dir,tag,f1,f2,energy,upg,spot,u1,u2,u3,f3,f4`。
  y/x 同为等距坐标、**y 在前 x 在后**。官方图字段 7/9/10 有非零值
  （`1,0,1,0,0`），语义未取证，CLI 暂写 0（TODO e2e）。
- **Waypoint 0 也有效**（sandwell 0-5 对应 6 玩家；CLI SetWaypoint 从
  base=0 起），CLI 跳过 (0,0) 空项。

## 已实锤：编码

- **[IsoMapPack5]** = `EncodeIsoMapPack5`（`code/rewrite/map/isomap.cpp`，
  与 oracle 对拍过的写链）+ base64 每行 70 字符、`key = 行号`（EA 编辑器
  MapData.cpp:1246-1275 同款）。
- **[OverlayPack]** = `EncodeF80(values, 262144, 32)` —— 512×512 表、32 段
  （EA 编辑器 MapData.cpp:1163 同款）。CLI 的 `EncodeF80` 见
  `code/rewrite/map/encode80.{h,cpp}`（encode80 命令集与 get_same 语义
  取自 shp_decode.cpp）。
- **cell 记录** = MAPFIELDDATA 11B：`u16 X + u16 Y + u16 Ground + bData[3] +
  bHeight + bData2[1]`（EA 编辑器实锤）。

## 待 e2e 裁决（2026-08-13 实测：游戏不写 rmcache）

**已确认：遭遇战流程游戏不写 `rmcache\RandMap.Map`**（存档 CONTENTS 只引用
`RANDMAP.SED` 种子文件，重载时按当前种子重新生成；`GUI:SaveMapMenu` 是
DebugLog 桩）。原版 .map 样本靠暂停菜单的 Save Map 拿到（2026-08-15 那三份
`RandMap_62298_after_gate*.Map` 即此来源）。

1. ~~**[Map] Size/LocalSize**~~：2026-10-06 已对拍闭合——游戏 RMG 捕获即
   `Size=0,0,74,82 / LocalSize=2,5,70,70`，与 CLI 逐字符一致。
2. ~~**[Basic] 节**~~：游戏 RMG 捕获只写 `Name=Random Map` +
   `MultiplayerOnly=1`，CLI 一致。（FA2 的 Player 键判单人图逻辑只影响
   编辑器渲染，游戏 RMG 图无此键。）
3. **[Houses] 节**：游戏 RMG 图不写，CLI 不写，一致。
4. ~~**OverlayPack/OverlayDataPack 段数/段头**~~：2026-10-06 闭合——双方都是
   32 段 (c,8192) format-80，菱形外字节完全一致。
5. **Structures 字段 7..16 的值**：游戏写什么（tag/f1/f2/energy/upg/spot
   语义）——见上方「仍待取证」第 1 条，需 MapType=3 活体捕获。
6. **`+0x119` unknown 字节**：IsoCell 里我们写 0。
7. **TileTypeClass +0x2E0（Morphable）**、建筑真实占地表、SetupLAT 行为。

## CLI 现状

- `ra2_rmg_cli --sed=… --dir=… [--out=…] [--seed=…] [--rules=…]`
- smoke（Seed=60556，雪地 70×70）与游戏实际参数（RandMap.Sed Seed=10791，
  温和）都跑通；test_cli 3 例 + 全量 ctest 42/42 绿。
- SEH 兜底：崩溃打印 code/地址/寄存器（`/DYNAMICBASE:NO` 下地址稳定可查 map）。
- 2026-08-13：坐标改为等距直写后，waypoint 输出如 `0=115091`（(115,91)，
  [0, W+H] 合法）；FA2 可正常打开（此前 ToReal 后 waypoint 落到地图外）。
- 2026-08-14：补入 `MarkStartInfluence`、修正 RecalcAttributes `0xFFFF` 无符号读回，
  并补写 `[OverlayDataPack]`（矿石量表）；已重新生成 seed=53393 的 `.map` 与 cells dump。
