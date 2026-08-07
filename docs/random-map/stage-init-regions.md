# 阶段 3：Init regions——把地图切成连通区域

> 状态：**四步流程与唯一取数点已实锤**；RegionClass 的字段语义部分未确认。
> 取证：`memory/data/decomp/rmg_init_regions.txt` / `rmg_region_helpers.txt` /
> `rmg_region_helpers2.txt`。
>
> 这一阶段**只抽 1 个随机数**，而且只在一条冷门分支上抽。它真正的价值是
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

// 3) 每个"陆地类"区域向外长几圈
for (i = 0; i < regionCount; ++i) {
    Region* r = regions[i];
    if (r->isWaterish == 0) continue;                 // +0x14
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

### `SeedRegion`（`0x58C800`）：BFS 涂一块，太小就并掉

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

选中的边界格之后被拿去找一个 `Level` 不同的邻格，作为"把区域 0 整体改判"的依据。

## 三、两个候选表函数

两个都返回「某个区域的边界格」列表（至少一个 8 邻域邻格不属于本区域），
只是遍历方式不同：

| 函数 | `this` | 遍历方式 | 用在 |
|------|--------|---------|------|
| `0x5A0700` | `MapSeedClass::Instance` | 扫工作数组 `side²` | `SeedRegion` 的抽签 |
| `0x58D410` | region 对象 | `MapClass` 的格子迭代器 | `Grow` / `FloodRest` |

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
| `+0x14` | `IsWaterOrShore(seed)` | 水/陆标志 |
| `+0x16` | seed 的 packed XY | 种子格 |
| `+0x1A` | 0（`Finalize` 会改写成它的参数） | **「已定型，不再切分」**——`0x58EBC0` 判定区域够小就置 1，`0x58D620` 见 1 直接返回 0 |
| `+0x1B` | 1 | 疑似「与外界连通」：`0x5905D0` 一条坡道都没开成时清 0，**读它的地方尚未找到** |
| `+0x1C` | 0 | **被切过几次**——`0x58D620` 每成功切一轮就 `+1`，没找到读它的地方 |
| `+0x20` | 0 | **分到的出生点个数**（阶段 6 写，见 [stage-starting-points.md](stage-starting-points.md)） |
| `+0x24` | 0 | **「已作废」**——`0x58D620` 把不足 101 格的碎块并进邻区后置 1，收尾时统一 `delete`（见 [stage-making-regions.md](stage-making-regions.md) §2.10/§2.11） |
| `+0x28` | `DynamicVectorClass<CellStruct>` 头，容量 100000 | **本区域的格子表**（数据 `+0x2C`、个数 `+0x38`） |

对象存进全局向量：数据 `[0xABDF94]`、个数 `[0xABDFA0]`、容量 `[0xABDF98]`、
向量对象 `[0xABDF90]`。

## 六、未确认

1. 格子 `+0x11B` 是不是 `Level`（高度层）——证据只有 `stage-init.md` 记的初值 4
   与 `MapSeedClass::Level` 默认值 4 吻合，以及这里"同层才算连通"的用法
3. `[0xABE2E4]`——`Level` 为 −1 时的兜底值，来源未确认
4. ~~`0x5A0700` 的容量预留 `[MapSeed+0x180] * [MapSeed+0x184]` 对应哪两个规则项~~
   ——**已关闭**：它俩不是规则项，就是地图宽 W 与高 H（见 §三）
5. RegionClass `+0x04`（已关闭：邻区表）/ `+0x1A`（已关闭：不再切分）/
   `+0x1B`（仍未确认：只见写不见读）的语义
6. 工作记录 `+0x4B`（`SeedRegion` 并区时清 0，`FloodRest` 要求它非 0）的含义
