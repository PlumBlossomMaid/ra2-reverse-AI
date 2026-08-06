# MapSeedClass 参数集与 .sed 文件格式

## 参数存盘：RandMap.Sed

游戏目录下的 `RandMap.Sed` 是 INI 文本，保存 RMG 对话框里的全部参数。
实机采样（用户操作游戏后生成）：

```ini
[RandomMap]
Description=968f,673a,5730,56fe,
Width=0
Height=0
NumPlayers=2
Seed=60556
MapType=1
Theater=1
Time=3
RegionSize=62
Ruggedness=64
Accessibility=40
WaterAmount=0
Tiberium=60
TiberiumLayout=29
Vegetation=78
UrbanPresence=12
Resources=3
```

**`Description` 是 UTF-16 码位的十六进制逗号列表**：
`968f,673a,5730,56fe` = `U+968F U+673A U+5730 U+56FE` = "随机地图"。

## 读写函数

| 函数 | 地址 | 证据 |
|---|---|---|
| `MapSeedClass::LoadMission`（读 `.sed`） | `0x597A30` | 16×`CCINIClass::ReadInteger` + 1×`ReadString`，节名 `"RandomMap"` @`0x82BB24` |
| `MapSeedClass::SaveMission`（写 `.sed`） | `0x597760` | 16×`WriteInteger` + 1×`WriteString` |
| `MapSeedClass::DeleteMission` | `0x597D50` | |
| `MapSeedClass::GetFileEntry` | `0x597D60` | `strcmp` `"RandMap.Sed"` / `"lastmap.sed"` |
| `MapSeedClass::GetUIString_Load/Save/Delete/GameSaved` | `0x597F80`/`FA0`/`FC0`/`FE0` | 调试宏行号 5572/5578/5584/5590 |

（函数名来自 YRpp 公开符号表，地址与本仓库汇编取证一致。）

## 字段偏移表

本仓库先由汇编独立还原，随后经 YRpp `MapSeedClass.h` **交叉验证一致**：

| 偏移 | 字段 | 说明 |
|---|---|---|
| `+0x38` | `Theater` | 地形主题 |
| `+0x3C` | `MapType` | 群岛/大陆等，默认 1 |
| `+0x40` | `Resources` | 资源丰富度，默认 1 |
| `+0x44` | `Ruggedness` | 崎岖度 |
| `+0x48` | `Time` | 时段（晨/午/夜），默认 1 |
| `+0x4C` | `WaterAmount` | 水量 |
| `+0x50` | `NumPlayers` | 玩家数，默认 2 |
| `+0x54` | `Tiberium` | 矿量 |
| `+0x58` | `TiberiumLayout` | 矿分布（TS 遗留，现随机化） |
| `+0x5C` | `Vegetation` | 植被 |
| `+0x60` | `UrbanPresence` | 城市化程度 |
| `+0x64` | `Width` | 宽 |
| `+0x68` | `Height` | 高 |
| `+0x6C` | `Accessibility` | 通达性 |
| `+0x70` | `RegionSize` | 区域尺寸 |
| `+0x74` | `Seed` | 随机种子，默认 -1 |
| `+0x78` | `DescriptionBuffer` | `FixedWString<0x80>`，占 0x100 字节，止于 `+0x178` |

`+0x178` 之后是 rules.ini 驱动的 RMG 配置（YRpp 标注）：
`RMGLevelLightSettings @+0x188`、`TemperateAmbientLight @+0x1A4`、…、
`RMGVegetationMinimums @+0x284` / `RMGVegetationMaximums @+0x2A0`、
`RMGMinimumTiberium`(默认 2500) / `RMGMaximumTiberium`(默认 5500)、
`TemperateOrePatchLamps @+0x2C4` / `SnowOrePatchLamps @+0x2E0`、
`MaxTrees`(默认 500)、`Level`(默认 4)。对象总长 `0x318`。

这些字段由 `FUN_00595740` 在构造后填充（先调 `MapSeedClass::MapSeedClass @0x595680`，
再连续 10 次调 `0x477DB0` / 2 次 `0x477BE0` 读列表型规则项）——
**说明生成结果同时受 rules.ini 影响，复刻时必须数据驱动，不能硬编码。**

## 相关文件

| 文件 | 角色 |
|---|---|
| `RandMap.Sed` | 当前参数（本文所述格式） |
| `lastmap.sed` | 上次参数 |
| `*.mmp` | 用户保存的参数预设（文件选择过滤器） |
| `RandMap.img` | **地图预览图**（体积随参数变化，非静态模板） |
| `rmcache\RandMap.Map` | 生成结果缓存路径（字符串 @`0x82BB44`） |
