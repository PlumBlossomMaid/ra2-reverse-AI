# theater（气候）数据：从 ini 到「起始砖号」

随机地图生成器判水、铺岸、涂草，靠的都是拿 `CellClass +0x38`
（`IsoTileTypeIndex`）去比一堆 `.bss` 全局。那些全局的值**不在 exe 里**，
是装载 theater 时从气候 ini 算出来的——这一节就是把这条链补齐。

补完它，阶段 2 里唯一还卡着的铺砖器 `0x57B440` 才有可能推进。

> RMG 只开放两种气候：**温和（temperate）** 与 **雪地（snow）**
>（游戏内实测下拉框就这两项）。

## 一、ini 从哪来

文件名模板在 `0x8295E8`：`"%sMD.INI"`，`%s` 取 theater 名表
（`0x7E1B78` 起，每条 0x70 字节）里的第 `+0x30` 个字段：

| theater | 名字 | ini |
|---|---|---|
| 0 | `TEMPERATE` / `TEMPERAT` | `TEMPERATMD.INI` |
| 1 | `SNOW` | `SNOWMD.INI` |

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

## 五、复刻状态

`code/rewrite/rmg/theater.{h,cpp}`：`ParseTheaterIni()` 把 ini 文本解析成
`TheaterData`（TileSet 列表 + 各语义组的起始砖号）。
单测 `test/test_theater.cpp` 用一段**手写**的同构 ini 验前缀和与 -1 语义，
并把上面那两条硬编码宽度写成断言——仓库里不放 theater 素材，
真值核对交给 `dump_tileset_starts.py` 在本机跑。

## 六、未确认

1. `TileTypeClass +0x294` / `+0x2E4` / `+0x2E8`（铺砖器 `0x57B440` 要读的
   footprint 尺寸与图像指针）还没还原——那要解析 `.tmp` 砖块文件本身，
   不是 ini 能给的
2. `LastTilesInSet` 键在两个 ini 里都**不存在**（`grep` 为空），
   但 `0x545FE5` 确实会读它并在与 `TilesInSet` 不等时走另一条分支。
   这条分支在原版数据下是死的，暂不追
3. `tileset_globals.txt` 里四个没匹配上键名的全局
   （`0xAA0E1C` / `0xAA1024` / `0xAA1090` / `0xAA10A4`）——本阶段用不到
