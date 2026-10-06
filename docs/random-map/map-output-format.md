# .map 输出格式（CLI 写链）

`code/rewrite/cli/mapwriter.{h,cpp}` 组装 RandMap.Map。本文记录已实锤的格式
与仍待 e2e 对拍裁决的部分。

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
DebugLog 桩）。原版 .map 样本拿不到，以下项以官方图 + FA2 实测为准。

1. **[Map] Size/LocalSize**：CLI 写 `Size=0,0,W+4,H+12`、`LocalSize=2,5,W,H`
   （stage-init.md §三）。官方手绘图的 `LocalSize=2,4,W−4,H−6` 与 Size 的关系
   不同（捡箱子 2,4,196,194 vs 200,200）——随机地图的取值以游戏产物为准。
2. **[Basic] 节**：FA2 依 `Player` 键判单人图（有键 → 单人渲染路径，无
   [Houses] 会野指针崩溃，MapData.cpp:6085）。CLI 写 `Name` + `MultiplayerOnly=1`
   （sandwell 同款）。其余键值（MinPlayer/MaxPlayer 等）未对齐。
3. **[Houses] 节**：sandwell 有 13 国家列表（编辑器下拉用）。CLI 未写，
   游戏实测"只有 2 玩家"与此关系待确认。
4. **OverlayPack/OverlayDataPack 段数/段头标志**：CLI 已同时写两节，均按 EA 编辑器
   32 段 F80；游戏实际段头标志是否一致仍待确认。
5. **Structures 字段 7..16 的值**：游戏写什么（tag/f1/f2/energy/upg/spot 语义）。
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
