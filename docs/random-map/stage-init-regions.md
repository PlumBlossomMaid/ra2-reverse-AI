# 阶段 3：Init regions——把地图切成连通区域

> 状态：**四步流程与唯一取数点已实锤，已落 C++ 并整段对拍通过**。
> 取证：`memory/data/decomp/rmg_init_regions.txt` / `rmg_region_helpers.txt` /
> `rmg_region_helpers2.txt`。
> 实现 `code/rewrite/rmg/regions.{h,cpp}`，基准 `memory/data/rmg/regions_golden.txt`
> （23 个用例，生成脚本 `code/randmap/dump_regions_golden.py`），见 §七。
>
> 这一阶段**只有 1 个取数点**，而且只在一条冷门分支上。它真正的价值是
> 建立起后面所有阶段都要用的「区域」概念——起始点、矿、山丘都挂在区域上。

## 一、它在流水线里的样子

主流程 `FUN_00598960` 在两条调试日志之间的这一段就是全部内容：

```c
// 1) 拆掉上一次的区域对象
for (i = regionCount - 1; i >= 0; --i)
    if (regions[i] != 0) DestroyRegion(regions[i]);   // 0x5AC290
regionCounter = 0;                                    // [0xABED14]

// 顺带把每格工作记录的两个区域字段清成 -1
for (each work record) { rec.regionId = -1; rec.queued = -1; }   // +0x38 / +0x3C

// 2) 按连通块建区域
CreateRegions();                                      // 0x58CF90

// 3) 每个 wet（水/岸/绿）区域向外长几圈
for (i = 0; i < regionCount; ++i) {                   // ⚠️ 上界每轮重读 [0xABDFA0]
    Region* r = regions[i];
    if (r->wet == 0) continue;                        // +0x14
    bool bigMap = (MapType == 3 || MapType == 4);
    Grow(4 + !bigMap + (r->cellCount > 8000));        // 0x58E740，参数 4/5/6
    FloodRest(r);                                     // 0x58E9B0
}

// 4) 剩下没归属的格子，再兜一遍
Finalize(0);                                          // 0x58D010
```

`Grow` 的圈数 `4 + !bigMap + (cellCount > 8000)`：

| 条件 | 圈数 |
|------|------|
| `MapType ∈ {3,4}` 且区域 ≤ 8000 格 | 4 |
| `MapType ∈ {3,4}` 且区域 > 8000 格 | 5 |
| 其他 `MapType` 且区域 ≤ 8000 格 | 5 |
| 其他 `MapType` 且区域 > 8000 格 | 6 |

`Grow` 的返回值被调度方**丢掉**（`0x598CE4` 之后没有 `test`），
所以它提前收工不算错误，`FloodRest` 照样跟着跑。

### ⚠️ `+0x14` 那道过滤器其实一个都拦不住

`0x598CAD cmp byte [esi+0x14], bl`（此处 `bl == 0`）+ `0x598CB0 je` = 「`+0x14`
为 0 就跳过本区域」。而**生效的** `+0x14` 不是构造函数写的那个值：

```
0058BF70  ...            ; ctor 里写 +0x14 = IsWaterOrShore(seed)
...
0058CE1D  mov al, [esp+0x1c]     ; wet = IsWaterOrShore(seed) || IsGreen(seed)
0058CE24  mov byte [esi+0x14], al  ; ← 调用方当场覆盖掉 ctor 那份
```

覆盖后的判据 `IsWaterOrShore || IsGreen` 与 `CreateRegions` 挑种子的判据
（`0x58CFC8` 那一串）**是同一条**。于是：

- `CreateRegions` 产出的每个区域必然 `wet == true`，第 3 步一个不落；
- 反过来，`SeedRegion` 里那三条「不足 75 格就并掉」的分支——**包含本阶段
  唯一的取数点 `0x58CAF9`**——只可能从第 4 步 `Finalize` 进去，
  因为只有 `Finalize` 会拿不满足该判据的格子起种。

取数点还额外要求 `id == 0`（即 `CreateRegions` 一个区域都没建出来），
这就是「1 个取数点、只在一条冷门分支上」的全部含义。

## 二、区域是怎么长出来的

### `CreateRegions` = 扫全图 + 对每个未归属格子起一次种（`0x58CF90`）

```c
for (int i = 0; i < side * side; ++i) {
    WorkRec& rec = work[i];
    Cell* c = MapClass::TryGetCellAt(rec.packedXY);
    if (rec.regionId != -1) continue;                 // 已经归属
    if (rec.packedXY == 0) continue;                  // X 和 Y 同时为 0 的哨兵格
    if (IsWaterOrShore(c) || IsGroupB(c))             // 0x4865D0 / 0x4867B0
        SeedRegion(c);                                // 0x58C800
}
```

⚠️ `rec.packedXY == 0` 这个条件写成 `(X != 0 || Y != 0)`——**左上角那一格 (0,0)
被无条件跳过**。它落在等距菱形之外，属于"照抄即可，不要修正"的一类。

### `SeedRegion`（`0x58C800`）：DFS 涂一块，太小就并掉

```c
int id = regionCounter;              // [0xABED14]，本次要用的编号
bool wet = IsWaterOrShore(seed) || IsGroupB(seed);
int level = seed->Level;             // +0x11B

queue = { seed->MapCoords };
int visited = 0;
while (!queue.empty()) {
    CellStruct xy = queue.pop_back();          // ← 后进先出，是 DFS 序
    work[xy].queued  = id;                     // +0x3C
    work[xy].regionId = id;                    // +0x38
    for (d = 0; d < 8; ++d) {
        CellStruct n = xy + AdjacentCell[d];   // 表在 0x89F688
        if (!InDiamond(n)) continue;
        if (work[n].regionId != -1) continue;  // 已归属
        if (work[n].queued == id) continue;    // 已在队列里
        work[n].queued = id;
        Cell* nc = MapClass::TryGetCellAt(n);
        if (nc->Level != level) continue;
        if ((IsWaterOrShore(nc) || IsGroupB(nc)) != wet) continue;
        queue.push_back(n);
    }
    ++visited;
}
if (visited > 0x4A) goto 建区域;               // 74 格以上才算数
```

不足 75 格的小块要被并掉；并的方式看 `wet`：

- `wet == true`（水/岸）：**直接建区域**，不并（跳到 `LAB_0058CDF6`）
- `wet == false`（陆地）且 `id != 0`：往西找一格（不在界内就往北），
  把整张图上 `regionId == id` 的格子全部改判成那一格所属的区域，
  同时把格子重置为 `Level = 邻格的 Level`（邻格 Level 为 −1 时用 `[0xABE2E4]`）、
  `IsoTileTypeIndex = 0`、`+0x11A = 0`、`work.+0x4B = 0`
- `wet == false` 且 `id == 0`：**没有"前一个区域"可并——这里才抽随机数**

### 唯一的取数点 `0x58CAF9`

```
0058CAD1  mov ecx, 0xABDFD8              ; MapSeedClass::Instance
0058CAD7  call 0x5A0700                  ; 取"区域 0 的边界格"列表
0058CAE6  mov esi, [ebp+0x10]            ; n = 列表长度
0058CAE9  dec esi                        ; hi = n - 1
0058CAF1  fild qword ptr [esp+0x34]      ; span = (double)(hi + 1) = n
0058CAF9  mov ecx, 0xABE890              ; ← 取数点
0058CAFE  call Randomizer::Random
0058CB0B  fild qword ptr [esp+0x34]      ; (double)(uint32)r，借 64 位 fild
0058CB0F  fmul qword ptr [esp+0x2C]      ; × span
0058CB13  fmul qword ptr [0x7ED898]      ; × 1/(2^32-1)
0058CB19  call 0x7C5F00                  ; _ftol（截断）
0058CB1E  cmp eax, esi
0058CB20  ja  0x58CAF9                   ; > hi 则重抽
0058CB22  mov ecx, [ebp+4]
0058CB27  mov edx, [ecx+eax*4]           ; 选中的格子坐标
```

就是 `PickRange(0, n-1)` 被内联展开，**乘法顺序 `r × span × (1/(2^32-1))` 与
`0x598030` 完全一致**（见 [fpu-and-random-idioms.md](fpu-and-random-idioms.md)）。

选中的边界格之后被拿去按 8 个方向找一个「`Level` 不同**且不是水岸**」的邻格；
找到就把整张图上 `regionId == 0` 的格子全刷回 `-1`（并把砖/子块清 0、
`Level` 压成那个邻格的 `Level`），返回"没建区域"。8 个方向全落空才落回建区域。

### ⚠️ 这条分支会把自己锁死——实测 631 个取数点里 611 个只有 1 个候选

重置那一段只刷 `+0x38`，**不动 `+0x3C`**，而 `+0x3C` 此刻正好等于 `0 == id`。
于是下一次起种（编号没被消耗，还是 `id == 0`）踩到这些格子时，会被
`work[n].queued == id` 那条当成"已入过队列"直接跳过——DFS 再也长不出去。

结果整条循环退化成「逐格重置」：每次 `SeedRegion` 只覆盖种子那一格，
`CollectBorderCells(0)` 只返回 1 个候选，`PickRange(0, 0)` 抽一个必中的数。
基准 `regions_golden.txt` 里 `syn-dry-*` 系列一共 631 个取数点，
候选表长度分布是 `1×611 / 2×11 / 3×5 / 11 / 15 / 24 / 43`——只有最靠前的
几次（重置还没污染 `+0x3C` 时）候选才多。

这也是为什么 `0x5A0700` 里那个 `break`（一格只入表一次）在现有基准上
**不可观测**：候选表长度几乎恒为 1，去掉 `break` 也还是 1。
它照抄自反汇编，但请注意它属于"有取证、无对拍"的一类。

## 三、两个候选表函数

两个都返回「某个区域的边界格」列表（至少一个 8 邻域邻格不属于本区域），
只是遍历方式不同：

| 函数 | `this` | 遍历方式 | 用在 |
|------|--------|---------|------|
| `0x5A0700` | `MapSeedClass::Instance` | 扫工作数组 `side²` | `SeedRegion` 的抽签 |
| `0x58D410` | region 对象 | `MapClass` 的格子迭代器 | `Grow` / `FloodRest` |

两者语义同义，但**实现细节有四处不同，一处都不能省**（都能改变产物）：

| # | `0x5A0700` | `0x58D410` |
|---|-----------|-----------|
| 1 | 扫工作数组，坐标取自记录自己存的 `+0x00`（`(0,0)` 的槽位整条跳过） | 走 `MapClass` 格子迭代器（`0x578350` 复位 + `0x578290` 步进），顺序完全不同 |
| 2 | 命中第一个"归属不同"的邻格就 `break` | **不 break**，8 个邻格全看完 |
| 3 | 纯算术加向量（`0x42D510`），且**先判界内**再 `GetCellAt` | 用 `CellClass::GetNeighbourCell`（`0x481810`，**带查表**）且**先取格再判界**——于是每一个界外邻格都往共享哑元的 `+0x24`（`[0xABDC74]`）写一次坐标 |
| 4 | 容量预留被改写成 `H*W` | 留在构造时的 `10` |

第 3 条是**可观测**的：把它挪成"先判界再取格"，对拍立刻在 52 个格子上炸掉
（哑元坐标终值是遍历顺序的指纹）。第 4 条则相反——`H*W` 只有在 W 或 H 为 0 时
才会让 `push` 静默失败，而正常地图不可能出现，属于"照抄、无对拍"。

`0x5A0700` 的容量预留是 `[this+0x184] * [this+0x180]`。
这两个字段**不是规则项**，是 [stage-init.md](stage-init.md) 算出来的
**地图真实宽 W（`+0x180`）与高 H（`+0x184`）**——写入点 `0x599700` / `0x599748`。
所以这里预留的就是"整张图的格子数"。
（旁证：`0x58EBC0` 用同一对字段算区域切分阈值
`_ftol(2·(RegionSize·0.005+0.05)·H·W)`，语义上只能是面积，
见 [stage-making-regions.md](stage-making-regions.md) §二。）

## 四、几个已实锤的谓词与工具函数

| 地址 | 语义 | 判据 |
|------|------|------|
| `0x4865D0` | 格子是水或岸砖 | `+0x38` 落在 `ShorePieces[0xABAD28, +0x2A)`（42 项）或 `WaterSet[0xAA0738, +0x0E)` / 四个瀑布 `WaterfallEast[0xAA073C, +4)`、`West[0xABB110, +4)`、`South[0xAA1050, +4)`、`North[0xAA10A0, +4)` |
| `0x4867B0` | **格子是绿地** | `+0x38 == GreenTile[0xAA0E18]` 或落在 `ClearToGreenLat[0xAA0748, +0x10)`（键名由 [stage-recalc.md](stage-recalc.md) §三 反查得到） |
| `0x486380` | 格子是"空地" | `+0x38 == 0xFFFF` 或 `== 0` |
| `0x5657A0` | `MapClass::TryGetCellAt` | `idx = Y*0x200 + X`，越界或空指针返回 `MapClass::InvalidCell`；格子指针数组在 `MapClass+0x13C` |
| `0x42D510` | 坐标加向量 | 就是 `xy + AdjacentCell[d]`，无边界检查 |

`Y*0x200 + X` 这一条顺带实锤了**引擎侧格子数组是 512×512 定长**，
与生成器自己那个 `side × side` 的工作数组是两套索引，不要混用。

## 五、RegionClass（`0x50` 字节，构造 `0x58BF70`）

| 偏移 | 构造时的值 | 推断 |
|------|-----------|------|
| `+0x00` | 0 | **候选点向量指针** `DynamicVectorClass<CellStruct>*`（数据 `+0x04`、个数 `+0x10`），阶段 6 填、阶段 8 用 |
| `+0x04` | 0 | **邻区编号表** `DynamicVectorClass<int>*`，只在阶段 4 存活（`0x58F0C0` 建、`0x5905D0` 用、当场销毁） |
| `+0x08` | `[0xABED14]`，随后计数器 +1 | **区域编号** |
| `+0x0C` | 0，建完后填成员格数 | 格子数 |
| `+0x10` | `seed->Level`（`+0x11B`） | 本区域的高度层 |
| `+0x14` | `IsWaterOrShore(seed)`，**但 `0x58CE24` 当场覆盖成 `IsWaterOrShore \|\| IsGreen`** | wet 标志（生效值是覆盖后那份，见 §一） |
| `+0x16` | seed 的 packed XY | 种子格 |
| `+0x1A` | 0（`Finalize` 会改写成它的参数） | **「已定型，不再切分」**——`0x58EBC0` 判定区域够小就置 1，`0x58D620` 见 1 直接返回 0 |
| `+0x1B` | 1 | 疑似「与外界连通」：`0x5905D0` 一条坡道都没开成时清 0，**读它的地方尚未找到** |
| `+0x1C` | 0 | **被切过几次**——`0x58D620` 每成功切一轮就 `+1`，没找到读它的地方 |
| `+0x20` | 0 | **分到的出生点个数**（阶段 6 写，见 [stage-starting-points.md](stage-starting-points.md)） |
| `+0x24` | 0 | **「已作废」**——`0x58D620` 把不足 101 格的碎块并进邻区后置 1，收尾时统一 `delete`（见 [stage-making-regions.md](stage-making-regions.md) §2.10/§2.11） |
| `+0x28` | `DynamicVectorClass<CellStruct>` 头，容量 100000 | **本区域的格子表**（数据 `+0x2C`、个数 `+0x38`） |

全局向量是**内联在 .bss 里的一个 `DynamicVectorClass`**，不是指针。
静态初始化器 `0x58BC30` 把它摆成：

| 地址 | 偏移 | 初值 | 含义 |
|------|------|------|------|
| `0xABDF90` | `+0x00` | `0x7ED858` | 虚表 |
| `0xABDF94` | `+0x04` | 0 | 数据指针 |
| `0xABDF98` | `+0x08` | 0 | 容量 |
| `0xABDF9C` | `+0x0C` | 1 | 「内存归自己所有」 |
| `0xABDFA0` | `+0x10` | 0 | 个数 |
| `0xABDFA4` | `+0x14` | **10** | 增长步长 |

增长步长这一条是硬要求：`DynamicVectorClass::push` 在步长 ≤ 0 时**静默失败**，
把它留成 0 的话区域一个都登记不上（模拟执行时踩过这个坑）。

## 六、MapSeedClass 就是静态对象 `0xABDFD8`——一批"来源不明"的全局当场对号

```
0059664C  mov ecx, 0xABDFD8 ; call 0x598960   ← 整条流水线的 this
0058CAD1  mov ecx, 0xABDFD8 ; call 0x5A0700   ← 取边界格的 this
```

于是之前散落在各阶段文档里、按绝对地址记下来的一堆全局，
其实都是**同一个对象的成员**：

| 绝对地址 | = `0xABDFD8 +` | 之前的记法 |
|----------|----------------|-----------|
| `0xABE014` | `+0x3C` | MapType |
| `0xABE024` | `+0x4C` | WaterAmount |
| `0xABE028` | `+0x50` | NumPlayers |
| `0xABE158` / `0xABE15C` | `+0x180` / `+0x184` | 地图宽 W / 高 H（§三 的容量预留） |
| `0xABE2E0` | `+0x308` | 块编号计数器（[stage-seeding-water.md](stage-seeding-water.md)） |
| **`0xABE2E4`** | **`+0x30C`** | **本阶段并区时 `Level` 为 −1 的兜底值** |
| `0xABE2F0..FC` | `+0x318..0x324` | 全图边界矩形 |
| `0xABE890` | `+0x8B8` | `Randomizer` |

`+0x30C` 在 [stage-seeding-water.md](stage-seeding-water.md) 里已经实锤为
「涂块回滚时写回 `+0x11B` 的层高」，默认 4——和 `MapSeedClass::Level` 默认值
以及 [stage-init.md](stage-init.md) 记的格子初始层高 4 三处吻合。
**所以下面第 3 条未确认关闭了**：`[0xABE2E4]` 就是 `MapSeedClass::Level`。

## 七、未确认

1. 格子 `+0x11B` 是不是 `Level`（高度层）——证据只有 `stage-init.md` 记的初值 4
   与 `MapSeedClass::Level` 默认值 4 吻合，以及这里"同层才算连通"的用法
3. ~~`[0xABE2E4]`——`Level` 为 −1 时的兜底值，来源未确认~~
   ——**已关闭**：`= MapSeedClass::Instance + 0x30C = MapSeedClass::Level`（见 §六）
4. ~~`0x5A0700` 的容量预留 `[MapSeed+0x180] * [MapSeed+0x184]` 对应哪两个规则项~~
   ——**已关闭**：它俩不是规则项，就是地图宽 W 与高 H（见 §三）
5. RegionClass `+0x04`（已关闭：邻区表）/ `+0x1A`（已关闭：不再切分）/
   `+0x1B`（仍未确认：只见写不见读）的语义
6. 工作记录 `+0x4B`（`SeedRegion` 并区时清 0，`FloodRest` 要求它非 0）的含义。
   已知它**能改变产物**：对拍时把 `FloodRest` 那条判定去掉，
   `syn-green-*` 立刻分岔——所以它不是装饰位，只是名字还没定。

## 八、C++ 落地与对拍

实现 `code/rewrite/rmg/regions.{h,cpp}`；对拍 `test/test_regions.cpp`
（ctest 目标 `regions`），基准 `memory/data/rmg/regions_golden.txt`。

基准由 `code/randmap/dump_regions_golden.py` 用 Unicorn 跑**原版机器码**取得，
一个桩都不留：两个谓词、两个边界格函数、`GetCellAt` 的哑元退化全是原身。
每个用例给一张输入地形（`IN` 行），跑完四步之后比五样：随机数调用次数、
区域表（个数/顺序/`id`/格数/层/wet/种子/`+0x1A`）、编号计数器 `[0xABED14]`
终值、每格终态 `(tile, subtile, level, +0x38, +0x3C)`、共享哑元坐标 `[0xABDC74]`。

23 个用例分两类：

- `s2-*` 6 例：先跑阶段 2 调度器 `0x59A6C0` 拿一张**真实地形**（含岸砖与层高），
  再跑阶段 3。覆盖 MapType 0/1/2/3 与三套砖尺寸剖面。
- `syn-*` 17 例：合成地形，专门去够阶段 2 到不了的分支。其中砖号全 1
  （既不是水/岸也不是绿）的 `syn-dry-*` 是唯一能让 `id == 0` 成立的构造，
  也是取数点 `0x58CAF9` 的**唯一**入口——最重的一例 `syn-dry-noise-a`
  一次跑出 **190 个取数点**，逐个对上。

变异测试确认下列细节**都有对拍覆盖**（改了就炸）：
`FloodRest` 的 `+0x4B` 判定、`Grow` 的提前放弃（`return false` 而非 `continue`）、
`Grow` 整体、`0x58D410` 的"先取格再判界"哑元副作用。
未被覆盖但有反汇编取证的：`0x5A0700` 的 `break`（见 §二）与它的 `H*W`
容量预留（见 §三）、`SeedRegion` 里 `visited > 0x4A` 的边界值 74/75
（基准里没有恰好 74 格的区域）。
