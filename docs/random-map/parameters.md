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
| `+0x38` | `Theater` | 地形主题。**RMG 只开放 0 = 温和、1 = 雪地**，见下文 |
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

### 构造函数写下的默认值（`0x595680`）

`LoadMission` 把**字段当前值**当作 `ReadInteger` 的 default 传进去，
所以「.sed 里缺哪个键」等价于「保留构造函数给的值」：

| 字段 | 默认 | 字段 | 默认 |
|---|---|---|---|
| `Theater` | 0 | `Tiberium` | 0 |
| `MapType` | **1** | `TiberiumLayout` | 0 |
| `Resources` | **1** | `Vegetation` | 0 |
| `Ruggedness` | 0 | `UrbanPresence` | 0 |
| `Time` | **1** | `Width` / `Height` | 0 |
| `WaterAmount` | 0 | `Accessibility` | 0 |
| `NumPlayers` | **2** | `RegionSize` | 0 |
| | | `Seed` | **-1** |

`Description` 缓冲区（`+0x78`）被 `mov word ptr [edi], ax` 置成空串。

### `ReadInteger` 的取值语义（`0x5276D0` 尾段）

不是简单的 `atoi`，是西木家 `INIClass::Get_Int` 那套，三条路径：

```asm
00527851  cmp  byte ptr [edx], 0x24        ; '$'
0052785b  push 0x825bb8                    ; "$%x"   -> sscanf
00527883  movsx edx, byte ptr [ecx+edx-1]  ; 末字符
00527889  call 0x7caff4                    ; tolower
00527891  cmp  eax, 0x68                   ; 'h'
0052789e  push 0x825bb4                    ; "%xh"   -> sscanf
005278be  call 0x7c9bfd                    ; 其余 -> atoi
```

与 RA1 `INI.CPP` 的 `Get_Int` 完全一致（[C&C 开源仓库](https://github.com/electronicarts/CnC_Remastered_Collection)），
又一次印证"RA1 是 RA2 的参考答案"。`.sed` 实际只写十进制，
但复刻要照抄——否则手写 `Seed=$1F` 这类输入会与原版分叉。

**复刻状态**：`code/rewrite/rmg/map_seed_params.{h,cpp}`，
回归 `test/test_map_seed_params.cpp`。

`+0x178` 之后是 rules.ini 驱动的 RMG 配置（YRpp 标注）：
`RMGLevelLightSettings @+0x188`、`TemperateAmbientLight @+0x1A4`、…、
`RMGVegetationMinimums @+0x284` / `RMGVegetationMaximums @+0x2A0`、
`RMGMinimumTiberium`(默认 2500) / `RMGMaximumTiberium`(默认 5500)、
`TemperateOrePatchLamps @+0x2C4` / `SnowOrePatchLamps @+0x2E0`、
`MaxTrees`(默认 500)、`Level`(默认 4)。对象总长 `0x318`。

这些字段由 `FUN_00595740` 在构造后填充（先调 `MapSeedClass::MapSeedClass @0x595680`，
再连续 10 次调 `0x477DB0` / 2 次 `0x477BE0` 读列表型规则项）——
**说明生成结果同时受 rules.ini 影响，复刻时必须数据驱动，不能硬编码。**

## Theater：RMG 只支持 2 种气候

游戏内的随机地图对话框只有**温和 / 雪地**两项（实机确认）。二进制侧三条独立佐证：

1. **rules.ini 的 RMG 配置键只有两套**，读取点 `0x59843E`-`0x598871`：
   `TemperateAmbientLight` / `SnowAmbientLight`、
   `Temperate/SnowAmbientRed|Green|Blue`、`TemperateOrePatchLamps` / `SnowOrePatchLamps`。
   **没有** Urban / Desert / NewUrban / Lunar 的对应项。
2. **阶段 10 只有两条分支**：`Theater == 0` 走温带专用的 `0x5A38C0` + `0x5A3AE0`，
   否则走 `0x5A4280`（见 [stage-lats.md](stage-lats.md)）——若真要支持 6 个 theater，
   不会是这种写法。
3. `0x5997C0` 处按 `"TEMPERATE" + Theater * 0x70` 索引 `TheaterTypeClass` 表
   （表基址 `0x7E1B78`，步长 `0x70`）。表本身**确实**有 6 项：

| idx | 名字 | ini 前缀 | ISO 前缀 | 后缀 |
|---|---|---|---|---|
| 0 | `TEMPERATE` | `TEMPERAT` | `ISOTEMP` / `ISOTEM` | `TEM` / `MMT` / `T` |
| 1 | `SNOW` | `SNOW` | `ISOSNOW` / `ISOSNO` | `SNO` / `MMS` / `A` |
| 2 | `URBAN` | … | … | … |

但 RMG 只会往里传 0 或 1。

> 复刻程序据此可以**只实现两条 theater 路径**，并对 `Theater ∉ {0,1}` 直接报错——
> 这不是简化，是与原版可达行为一致。

## 相关文件

| 文件 | 角色 |
|---|---|
| `RandMap.Sed` | 当前参数（本文所述格式） |
| `lastmap.sed` | 上次参数 |
| `*.mmp` | 用户保存的参数预设（文件选择过滤器） |
| `RandMap.img` | **地图预览图**（体积随参数变化，非静态模板） |
| `rmcache\RandMap.Map` | 生成结果缓存路径（字符串 @`0x82BB44`） |
