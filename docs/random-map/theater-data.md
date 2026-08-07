# theater（气候）数据：从 ini 到「起始砖号」

随机地图生成器判水、铺岸、涂草，靠的都是拿 `CellClass +0x38`
（`IsoTileTypeIndex`）去比一堆 `.bss` 全局。那些全局的值**不在 exe 里**，
是装载 theater 时从气候 ini 算出来的——这一节就是把这条链补齐。

补完它，阶段 2 里唯一还卡着的铺砖器 `0x57B440` 才有可能推进。

> RMG 只开放两种气候：**温和（temperate）** 与 **雪地（snow）**
>（游戏内实测下拉框就这两项）。

## 一、ini 从哪来

文件名模板在 `0x8295E8`：`"%sMD.INI"`，`%s` 取 theater 名表
（`0x7E1B78` 起，每条 0x70 字节）里的第 `+0x30` 个字段。
整条记录已按字节读出来，本项目要用的五个字段是：

| 偏移 | temperate | snow | urban | 用途 |
|---|---|---|---|---|
| `+0x00` | `TEMPERATE` | `SNOW` | `URBAN` | 气候名 |
| `+0x10` | `Name:Temperate` | `Name:Snow` | `Name:Urban` | UI 串键 |
| `+0x30` | `TEMPERAT` | `SNOW` | `URBAN` | ini / mix 前缀 |
| `+0x3A` | `ISOTEMP` | `ISOSNOW` | `ISOURB` | 基础砖库名 |
| `+0x44` | `ISOTEM` | `ISOSNO` | `ISOURB` | 扩展包砖库名 |
| `+0x4E` | `TEM` | `SNO` | `URB` | 砖文件扩展名 |

五个 mix 名的拼装点连在一起，在 `0x5349EA`-`0x534A3A`，都走同一个 sprintf
函数指针 `[0x7E14B0]`：

```asm
005349d7  lea esi, [edi*8] / sub esi, edi / shl esi, 4   ; esi = theater * 0x70
005349e3  lea ebx, [esi + 0x7e1ba8]                      ; +0x30 -> "%s.MIX"    TEMPERAT.MIX
005349f2  lea ecx, [esi + 0x7e1bb2]                      ; +0x3A -> "%s.MIX"    ISOTEMP.MIX
00534a05  lea eax, [esi + 0x7e1bc6]                      ; +0x4E -> "%s.MIX"    TEM.MIX
00534a1c  lea ecx, [esi + 0x7e1bbc]                      ; +0x44 -> "%sMD.MIX"  ISOTEMMD.MIX
00534a2f  push ebx                                       ; +0x30 -> "%sMD.MIX"  TEMPERATMD.MIX
```

> ⚠️ **两份砖库谁优先，未确认。** 五个名字是一起造好的，注册顺序没查。
> 之所以不影响复刻：实测两份归档**几乎不重叠**——`isotemmd.mix` 45 条对
> `isotemp.mix` 只有 2 条同名，`isosnomd.mix` 178 条对 `isosnow.mix` 同样只有
> 2 条，而那 2 条都不是 `.tmp`。也就是说 md 包只**新增**砖、不覆盖砖，
> 顺序取哪个结果都一样。

| theater | ini |
|---|---|
| 0 | `TEMPERATMD.INI` |
| 1 | `SNOWMD.INI` |

两个 ini 都封在 `ra2md.mix` → `localmd.mix` 里。本仓库为此写了一个只读的
mix 解析器 `code/mix/mix_read.py`（见 [../mix-format/README.md](../mix-format/README.md)），
**只读不写，也不把 theater 素材带进仓库**。

## 二、装载：`FUN_00545150` 的两段

### 第一段：读键 -> 栈槽（`0x545570` 起）

几十次 `FUN_005276D0("General", <键名>, -1)`，把 ini `[General]` 节里
各个语义键对应的**组序号**读进栈上的局部变量。

### 第二段：枚举 TileSet -> 回填全局（`0x545C6B`-`0x545FA3`）

按 `TileSet0000`、`TileSet0001`、…… 的顺序枚举，每轮：

```c
// ebx 进入本轮时 = 前面所有组的 TilesInSet 之和 = 本组的起始砖号
if (edi /* 本组序号 */ == stack[键 A]) *globalA = ebx;
if (edi == stack[键 B]) *globalB = ebx;
... 四十多条 ...
ebx += TilesInSet;   // 累加进下一组
```

**于是全局里存的是起始砖号，不是组序号**——这是最容易搞错的一点。
换句话说：

```
start(k) = Σ TilesInSet[0 .. k-1]
```

一条前缀和。「全局 ↔ ini 键名」的对照表（45 条）见
`memory/data/rmg/tileset_globals.txt`，算出来的起始砖号见
`memory/data/rmg/tileset_starts.txt`（生成脚本
`code/randmap/dump_tileset_starts.py`）。

### ⚠️ `TilesInSet = 0` 的组不占砖号

`TileSet0001`（Misc Buildings）这类空组照样参与枚举，只是不推进 `ebx`。
所以相邻两组的起始砖号可以相同。

## 三、交叉验证：两条硬编码宽度对上了

这条推断能立住，靠的是 exe 里两处**硬编码的区间宽度**恰好等于对应组的
`TilesInSet`：

| 硬编码 | 出处 | 对应组 | ini 里的 `TilesInSet` |
|---|---|---|---|
| `+0x0E` = 14 | `IsWater 0x485060`：`tile ∈ [[0xAA0738], +0x0E)` | WaterSet | **14** |
| `+0x2A` = 42 | 涂块收尾的岸砖清扫 `[ShorePieces, +0x2A)` | ShorePieces | **42** |

两条独立命中，而且温和与雪地这两组的砖数**完全一样**（都是 14 / 42）——
所以西木敢把宽度写死在代码里。

## 四、算出来的关键值

| 全局 | ini 键 | temperate 起始砖 | snow 起始砖 |
|---|---|---|---|
| `0xAA0738` | WaterSet | 314 | 314 |
| `0xABAD28` | ShorePieces | 89 | 89 |
| `0xAA0E18` | GreenTile | 493 | 713 |
| `0xAA0748` | ClearToGreenLat | 494 | 714 |
| `0xAA10B0` | ClearTile | 0 | 0 |
| `0xABC2B8` | RoughTile | 131 | 131 |
| 全图砖数 | — | 838 | 964 |

> 有意思的是**水与岸在两种气候里砖号完全相同**（314 / 89），
> 而草地差了 220——雪地在草地之前多插了一大段冰面与雪地专用组。
> 判水逻辑因此跨气候完全一致，判草则不然。

## 四之二、缺 `.tmp` 是常态，原版把尺寸写 0

枚举出来的砖号有一大截**根本没有对应文件**：温和 838 块里缺 343 块（20 个组），
雪地 964 块里缺 417 块。缺的全是这类组：

```
TileSet0007  ZMM Ramps                 mslop   20 块
TileSet0008  Obsolete Height Pieces    hyte     7 块
TileSet0022  ZMM Cliff Set             Mclif   40 块
TileSet0061  ZMM Shore Pieces          MShore  42 块
TileSet0069  ZMM Water                 MWater  14 块
TileSet0080  Wood Bridges              Ovrpsb  16 块
...
```

`ZMM` = Marble Madness，是地图编辑器那个"大理石视图"用的替身砖；`Obsolete`
顾名思义。这些组照样占砖号（`TilesInSet` 非 0，会推进前缀和），只是没素材。

原版的处理在装载循环 `0x546B10` 的末尾：

```asm
00546b38  test ebx, ebx                       ; ebx = .tmp 映像指针
00546b40  je   0x546b8b
00546b46  mov al,  byte ptr [ebx]             ; BlocksX，只取低字节
00546b4a  mov dword ptr [ebp + 0x2e4], eax
00546b50  mov cl,  byte ptr [ebx + 4]         ; BlocksY，同样只取低字节
00546b53  mov dword ptr [ebp + 0x2e8], ecx
...
00546b8b  mov eax, dword ptr [esp + 0x18]     ; 已装载的变体计数
00546b8f  test eax, eax
00546b91  jne  0x546ba7
00546b93  mov dword ptr [ebp + 0x2e4], 0      ; ← 第一次就没拿到映像：宽高写 0
00546b9d  mov dword ptr [ebp + 0x2e8], 0
```

即：**第一次装载就拿不到映像时 `BlocksX = BlocksY = 0`**。
配套的取值端 `0x544BE0`（`TileTypeClass::GetGroundType`）也就此收口：

```c
undefined4 FUN_00544be0(int *this, int sub) {
  int *img = (int *)(**(code **)(*this + 0x9c))();          // 取 .tmp 映像
  if (img != NULL) {
    if (img[sub % (img[1] * img[0]) + 4] != 0) {            // ← 取模！空洞为 0
      return *(&DAT_008288e4 + *(char *)(img[sub % (img[1] * img[0]) + 4] + 0x29) * 4);
    }
  }
  return 0;                                                 // 映像空 / 空洞 -> 0
}
```

三条怪癖一并记下，复刻时照抄：

1. **子格下标按 `BlocksX * BlocksY` 取模**，越界不崩、会绕回去。
   而且这里乘的是**完整的两个 dword**，不是写进 `+0x2E4/+0x2E8` 的低字节版。
2. **空洞（偏移表项为 0）与映像为空都返回 0**，也就是 `Ground[0]` = Clear。
3. `+0x29` 是 `movsx` 读的**有符号**字节（真实数据里都落在 0..15）。

> `TilesInSet == 0` 的组不推进前缀和，于是它与紧随其后的组**共用同一个起始砖号**
> ——温和的 `TileSet0011`「Civilian Buildings」就正好压在 `ShorePieces` 的 89 上。
> 所以"按起始砖号反查组内砖数"这种查询天生有歧义，得跳过空组。
> 原版从不做这个查询（它把 14 / 42 硬编码在代码里），这是本项目自查用的辅助函数
> `TheaterData::TilesInSetStartingAt` 踩到的坑，已修。

## 五、复刻状态

三层，各管一段：

| 层 | 位置 | 管什么 |
|---|---|---|
| ini 解析 | `code/rewrite/rmg/theater.{h,cpp}` | ini 文本 → `TheaterData`（TileSet 列表 + 各语义组的起始砖号）。纯计算，不碰文件 |
| 容器解析 | `code/rewrite/mix/` | `.mix` 归档（Blowfish + RSA）与 `.tmp` 砖头。见 [../mix-format/README.md](../mix-format/README.md) |
| 装载 | `code/rewrite/assets/theater_assets.{h,cpp}` | 把上面两层串起来：给一个原版目录，吐出整套逐砖几何 |

**对拍强度：与 Python 侧独立实现逐字段一致。**
`test_theater_assets.cpp` 拿 `memory/data/rmg/tile_geometry.txt`
（由 `code/randmap/dump_tile_geometry.py` 生成）比对 C++ 装载器现读的结果：
文件名、`BlocksX/BlocksY`、每个子格的 `present/height/ramp/terrain` 全比。
两套实现从 mix 头解密、名字哈希、`.tmp` 头解析到前缀和都是各自写的，
四处任一出错都会被抓住。另外还断言了：

- 全图砖数 838 / 964，`WaterSet` 314 / 314，`ShorePieces` 89 / 89，
  `GreenTile` 493 / 713，`ClearTile` 0，`RoughTile` 131
- 两条硬编码宽度 14 / 42
- 温和缺文件的砖**恰好 343 块**，且它们的 `BlocksX/BlocksY` 都是 0、
  `GroundTypeOf` 都返回 0
- `GroundTypeOf` 的子格下标取模行为（`s` 与 `s + 3n` 同值）

`test/test_theater.cpp` 另有一套**手写**同构 ini 的用例，验前缀和与 -1 语义
——这一套不需要游戏数据，任何机器都跑。需要真档的用例统一靠环境变量
`RA2_GAME_DIR` 开关，没设就 `SKIP`；仓库里一份 theater 素材都不放。

## 六、未确认

1. ~~`TileTypeClass +0x294` / `+0x2E4` / `+0x2E8`~~ ✅ 已还原（2026-08-06）：
   `+0x294` 是砖号（构造函数 `0x5447D9` 从参数直接存），
   `+0x2E4` / `+0x2E8` 是 `.tmp` 文件头 `+0x00` / `+0x04` 两个**字节**
   （`0x546B46`-`0x546B53`，`mov al` / `mov cl`）。布局与实测见
   [stage-seeding-water.md](stage-seeding-water.md) §七「砖块尺寸从哪来」。
   `.tmp` 从 `ra2.mix` → `isotemp.mix` 里取，用 `code/mix/mix_read.py` 读
2. `LastTilesInSet` 键在两个 ini 里都**不存在**（`grep` 为空），
   但 `0x545FE5` 确实会读它并在与 `TilesInSet` 不等时走另一条分支。
   这条分支在原版数据下是死的，暂不追
3. `tileset_globals.txt` 里四个没匹配上键名的全局
   （`0xAA0E1C` / `0xAA1024` / `0xAA1090` / `0xAA10A4`）——本阶段用不到
4. 贴砖器又牵出六个之前没登记的组，现已补进 `TheaterData`：
   `CliffSet 0xAA1020` / `CliffRamps 0xABBEBC` / `RampSmooth 0xAA1058` /
   `WaterCaves 0xABAD24` / `WoodBridgeSet 0xABAD1C` / `DestroyableCliffs 0xABC2C8`。
   其中 `WaterCaves` 与 `WoodBridgeSet` 在 temperate 里没定义（值为 -1），
   而两份地形保护判据里只有一份会查 -1——差异见
   [stage-seeding-water.md](stage-seeding-water.md) §七「两份判据不一致」
