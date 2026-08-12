# .map 输出格式（CLI 写链）

`code/rewrite/cli/mapwriter.{h,cpp}` 组装 RandMap.Map。本文记录已实锤的格式
与仍待 e2e 对拍裁决的部分。

## 已实锤：坐标语义（2026-08-12）

**Waypoints / Terrain / Structures 全部使用"真实坐标"**（非等距格坐标）。
两条独立证据（本机官方图，FinalAlert2 编辑器制作）：

- `E:\YRLauncher\捡箱子.yrm`（[Map] Size=0,0,200,200）：
  `[Waypoints] 1=202364` → PosToXY 解析 x=202、y=364 —— y 超出 200 地图，
  等距坐标不可能，真实坐标范围（约 [0, 398]）才放得下。
- `E:\YRLauncher\sandwell.yrm`（[Map] Size=0,0,100,150）：
  `[Terrain] 93179=TIBTRE02` → y=179 > 150，同样只能是真实坐标；
  `[Structures] 0=Neutral,CAOILD,256,91,45,64,...` → 第 4 字段 y=91、第 5 字段 x=45
  （health 之后是 **y 在前 x 在后**）。

等距 → 真实换算（map-geometry.md §一，`ToReal`）：

```
2rx = X − Y + W − 1    2ry = X + Y − W − 1
```

其中 W = `[0x87F8DC]`（= 真实宽 + 4，含边界余量）。**半数菱形格落在半整数
真实坐标上，整数除法（向零）的取舍由 e2e 对拍裁决**（TODO）。

## 已实锤：键/值编码

- **键（Waypoints 的键、Terrain 的键）** = `"x" + "y 补零 3 位"`。
  EA 编辑器 `PosToXY`（functions.cpp:692）实锤：后 3 字符是 y、其余是 x，
  各自 `atoi`。例：`121113` → (121, 113)。
- **Structures 值** = 17 字段逗号表，顺序（GetParam 0..16，MapData.cpp:1650）：
  `owner,type,health,y,x,dir,tag,f1,f2,energy,upg,spot,u1,u2,u3,f3,f4`。
  官方图字段 7/9/10 有非零值（`1,0,1,0,0`），语义未取证，CLI 暂写 0（TODO e2e）。
- **Waypoint 0 也有效**（随机地图固定有 0 号出生点），CLI 跳过 (0,0) 空项。

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

## 待 e2e 裁决（开游戏生成 RandMap.Map 逐字节对拍）

1. **[Map] Size/LocalSize**：CLI 写 `Size=0,0,W+4,H+12`、`LocalSize=2,5,W,H`
   （stage-init.md §三）。官方手绘图的 `LocalSize=2,4,W−4,H−6` 与 Size 的关系
   不同（捡箱子 2,4,196,194 vs 200,200）——随机地图的取值以游戏产物为准。
2. **[Basic] 节**：游戏生成的 RandMap.Map 是否带 `[Basic]`（我们没写）。
3. **OverlayPack 段数/段头标志**：EA 编辑器 32 段与游戏实际是否一致。
4. **半整数真实坐标**：`ToReal` 的整数除法与游戏的实际编码（可能不同取舍）。
5. **Structures 字段 7..16 的值**：游戏写什么（tag/f1/f2/energy/upg/spot 语义）。
6. **`+0x119` unknown 字节**：IsoCell 里我们写 0。
7. **TileTypeClass +0x2E0（Morphable）**、建筑真实占地表、SetupLAT 行为。

## CLI 现状

- `ra2_rmg_cli --sed=… --dir=… [--out=…] [--seed=…] [--rules=…]`
- smoke（Seed=60556，雪地 70×70）与游戏实际参数（RandMap.Sed Seed=10791，
  温和）都跑通；test_cli 3 例 + 全量 ctest 41/41 绿。
- SEH 兜底：崩溃打印 code/地址/寄存器（`/DYNAMICBASE:NO` 下地址稳定可查 map）。
