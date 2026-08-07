# 阶段：Seeding water（水域播种）

> 状态：**涂块核心 `0x59BBC0` 已于 2026-08-06 整函数落成 C++ 并对拍通过**
> （`code/rewrite/rmg/blob_paint.{h,cpp}` / `test_blob_paint.cpp`，14 组用例、
> 约一万格**逐格生长顺序**与原版完全一致，随机数取数次数也逐组吻合，见 §四末尾）；
> 岸线平滑的**八邻域掩码 `0x57B210`**（§七，1299 次查询）与
> **区域网格 `0x59A8F0`**（§三，16 组用例）也已落成并对拍。
> 三种 MapType 的**投放策略**（§二末，12 组用例）、theater 数据链（见
> [theater-data.md](theater-data.md)）与**贴砖器 `0x57B440`**（§七，21 组用例）
> 也都已落成并对拍；四遍平滑的编排（§七）与**阶段调度器 `0x59A6C0` 本身**
> （§一末尾）同样已落成并对拍——`MapType ∈ {0,1,2}` 的主路径整条闭合。
> 地图容器见 [map-geometry.md](map-geometry.md)。
> 本文只记录已取证的部分，推测一律标注「未确认」。
> 涉及的数学基元见 [fast-math.md](fast-math.md)（已逐位对拍）。
>
> §八的**随机数消耗账本**是本阶段最要紧的一节：种子兼容性的全部内容就是
> "阶段顺序 + 每阶段取数个数"，账错一个数，之后每一格地形都对不上。

## 一、这个阶段其实是「从整张水里抠出陆地」

`FUN_0059A6C0`（调度，`RMG: Seeding water` 日志之后第一个调用）开头：

```c
for (cell : MapClass::Instance)         // 0x578350 Reset / 0x578290 Next
    cell->IsoTileTypeIndex = [0xAA0738];   // 全图先铺成水
```

`CellClass::IsoTileTypeIndex` 在 `+0x38`（YRpp 布局：AbstractClass 0x24 +
MapCoords 4 + FoggedObjects 4 + BridgeOwnerCell 4 + unknown 4 + LightConvert 4 = 0x38 ✅）。
`[0xAA0738]` 是水面 tile 组的起始索引（运行期由 theater 决定，**未确认**具体来源）。

所以"Seeding water"名不副实：**地图初态就是一片汪洋，后续是在水里画陆地**。
这也解释了为什么原版随机地图容易出岛——陆地是"画"出来的，画不到就是海。

### 调度器 `FUN_0059A6C0` 的八步全貌（✅ 已实锤）

```c
void MapSeedClass::SeedWater()                      // 0x59A6C0, ret 0
{
  for (cell : Map) cell->IsoTile = [0xAA0738];      // (a) 0x59A6DF 全图铺水

  switch (this->MapType /* +0x3C */) {              // (b) 0x59A6F6
    case 0: FUN_0059AD10(this); break;
    case 1: FUN_0059AFA0(this); break;
    case 2: FUN_0059B200(this); break;
    // 其余取值什么都不做（0x59A702 的 jne 直接落到 (c)）
  }

  for (cell : Map) {                                // (c) 0x59A737 孤水格回填
    if (cell->IsoTile != [0xAA0738]) continue;      //     ⚠️ 等值比较，不是判水区间
    bool all = true;
    for (int d = 0; d < 8; d += 2)                  //     只看四个正方向
      if (!IsUndecided(cell->Adjacent(d))) all = false;   // ⚠️ 无短路
    if (all) cell->IsoTile = 0;
  }

  Map->Shoreline(0, 0);                             // (d) 0x59A77F ⚠️ flag = 0
                                                    //     返回值被丢弃
  for (i = 0; i < side*side; ++i)                   // (e) 0x59A7A5
    work[i].+0x38 = work[i].+0x3C = -1;             //     ⚠️ 写 -1 不是 0

  destroy all RegionClass in [0xABDF94];            // (f) 0x59A7C2 不碰地形
  [0xABED14] = 0;                                   // (g) 0x59A87A 区域编号归零

  for (cell : Map) {                                // (h) 0x59A899 沿岸刷绿
    if (!IsShorePiece(cell)) continue;
    for (int d = 0; d < 8; d += 2) {
      CellClass* n = cell->Adjacent(d);
      if (IsUndecided(n)) n->IsoTile = [0xAA0E18];  //     ⚠️ 写的是**邻格**
    }
  }
}
```

三处必须照抄的细节：

1. **(c) 的判水是等值比较**。`0x59A73F` 拿 `cell->+0x38` 直接和 `[0xAA0738]`
   比相等，不是别处那个 `[base, base+0x0E)` 区间。也就是说只有"刚铺完水、
   还没被岸线改过"的格子才进得来。
2. **(c) 与 (h) 的邻格可能是共享哑元**。`0x481810` 走 `0x5657A0` 查指针表，
   出图就退到静态哑元 `0xABDC50`。(c) 只读它，(h) 会**写**它——哑元一旦被写成
   `GreenTile` 就不再"未定砖"，此后所有出图邻格都会被跳过。这是可观测状态。
3. **(d) 传的 flag 是 0**，与涂块收尾（`0x59C433`）传的 1 不是一回事，见 §七。

### 复刻状态：整条链已落成并对拍（✅ 2026-08-06）

`code/rewrite/rmg/stage_water.{h,cpp}` + `test/test_stage_water.cpp`，
基准 `memory/data/rmg/stage_water_golden.txt`（生成脚本
`code/randmap/dump_stage_water_golden.py`）。

这是目前最"整"的一次对拍：**一次调用把铺水 → 投放策略 → 涂块核心 →
岸线四遍 → 掩码 → 贴砖器整条链的真身全跑一遍**，模拟执行侧只留
`IsPaintable`（`0x4865D0`）一个桩。12 组用例（三种 MapType × 两套砖尺寸剖面 ×
不同玩家数/WaterAmount/可用区，外加 MapType 3 的"没人涂陆地"退化例）比对：
随机数调用次数、收尾块编号、共享哑元的 `+0x38`、工作数组的 `+0x38/+0x3C`、
以及菱形内每一格的 `(砖号, 子格号, 层高, 块归属)`。

对拍顺手确认了一条**看着像 bug 其实是原版行为**的事实：MapType 0 在小图上
（本文用 10×10）每一块陆地都会因为铺不出岸线而整块回滚，**终态全是水**。
基准里 `mt0-allwater` 一例专门盯住它。地图放大到 20×20 就正常出陆地了。

## 二、三种 MapType 三套投放策略

```c
switch (MapSeedClass->MapType /* +0x3C */) {
  case 0: FUN_0059AD10(); break;   // 网格分区，每区一块
  case 1: FUN_0059AFA0(); break;   // 从地图中心贪心生长到目标覆盖率
  case 2: FUN_0059B200(); break;   // 一刀切两半，每半各自生长
}
// MapType 3/4 走另一条路：FUN_0059C580 里判断 (MapType==3||4) && WaterAmount>0x14
```

三个分支共用涂块核心 `FUN_0059BBC0`（§四），差别只在**投放哪里、投多少、投几次**。
三者开头都做同两件事：`MapSeedClass[0x308] = 1`（块编号从 1 开始），
以及遍历全图把工作记录 `+0x3C` 清零（清掉上一阶段的入堆标记）。

一个贯穿三者的常数事实：**陆地目标占比都在 45%~50% 附近**（见下），
与"地图初态是一片汪洋"合起来看，原版的设计意图就是**海陆各半**。

### 全图边界的两组全局

| 地址 | 含义 |
|------|------|
| `0x87F8DC` / `0x87F8E0` | 地图宽 `W` / 高 `H` |
| `0x87F8E4` `0x87F8E8` `0x87F8EC` `0x87F8F0` | 可用区域 `{x, y, w, h}`（真实坐标） |

### MapType 0（`FUN_0059AD10`）：网格分区，每区一块

```c
int n = max(NumPlayers /* +0x50 */ / 2, 2);
int r = PickRange(1, n);                      // ⚠️ 下界是 1，不是 0
MakeGrid(&list, NumPlayers + r, &Usable);     // 0x59A8F0，见 §三
for (cell : Map) work[cell].queued = 0;

while (!list.empty()) {
    Rect rc = list.front();
    // 预算 = 区域格数的 45%~50%
    double u  = (double)(uint32)Random() * (1.0/4294967295.0);
    int budget = (int)((u * 0.05 + 0.45) * (rc.w * rc.h * 2));   // 0x7E8AE8 / 0x7ED990
    // 区域中心 -> 等距坐标
    int X = (rc.x + rc.w/2) + (rc.y + rc.h/2) + 1;
    int Y = (rc.y + rc.h/2) - (rc.x + rc.w/2) + W;
    for (int k = 0; k < 10; ++k) {                     // 最多试 10 次
        ok = Paint(budget, &rc, {X,Y}, 1/*椭圆*/, &{X,Y}, 0.25, 0/*距离+抖动*/);
        if (!ok) MapSeedClass[0x308]++;                // 失败也占掉一个块编号
        if (ok) break;
    }
    list.erase(list.begin());                          // 整体前移一格
}
```

`rc.w * rc.h * 2` 里的 `×2` 是等距坐标下的格数换算：真实坐标 `w×h` 的矩形
在等距网格里占 `2wh` 格。所以预算就是**区域格数的 45%~50%**。

注意 `Paint` 失败时 `[0x308]` 照样自增——**块编号会跳号**。复刻时不可"优化"掉。

> 🔧 **自我纠错（2026-08-06）**：此处原先写的是 `PickAtMost(n)`、`r ∈ [0, n]`。
> 逐条核对 `0x59AD6E-0x59AD9B` 后确认取数是
> `_ftol(r * n * 2.3283e-10 + 1.0)`（`0x59AD8E` 那句 `fadd [0x7E1718]`
> 加的就是 1.0），拒绝条件 `ja n` 是无符号——所以 **`r ∈ [1, n]`**，
> 取不到 0。投放区个数因此至少是 `NumPlayers + 1`。

### MapType 1（`FUN_0059AFA0`）：从地图中心贪心生长

```c
double total  = (double)CountUsableCells();  // 0x42B1F0 = (H + 4) * W * 2
double target = (0.5 - 0.45) * (1.0 - WaterAmount /* +0x4C */ * 0.01) + 0.45;
int    cap    = (int)(total * 0.03 * target);                      // 单次预算上限
Cell   center = { W/2 + H/2 + 1, W/2 + H/2 };                      // 地图正中的等距坐标
Rect   region = { bx+1, by+1, bw-2, bh-2 };                        // 全图内缩 1

int painted = 0;
for (int iter = 0; target > 0.0 && iter < 100; ++iter) {
    int budget = min((int)((target - painted/total) * total), cap);  // 还差多少要多少
    painted += Paint(budget, &region, &start, 1/*椭圆*/, &MapCentre, 0.75, 1/*方向性*/);
    // 下一个起点 = 全图中「+0x3C == 0 且到**地图正中**曼哈顿距离最小」的格子
    start = argmin_{cell: work[cell].queued == 0}
                  (|cell.Y - MapCentre.Y| + |cell.X - MapCentre.X|);
    if (painted / total >= target) break;
}
```

#### ⚠️ 只有起点在动，参照点和目标点都焊死在地图正中

`MapCentre` 在 `0x59B030` 算出来之后就留在 `ebx`/`ebp` 里，**整个循环再没被改过**：

- `0x59B119` / `0x59B11E` 拼给 `Paint` 的目标点用的是 `bp`/`bx`，即原始地图正中；
- `0x59B19D` / `0x59B1A8` 求最近格时减的也是 `ebx`/`ebp`。

被更新的只有 `[esp+0x28]/[esp+0x2C]`——那是**起点**。`0x59BBC0` 会保存
`ebx`/`ebp`（`0x59BBCF`/`0x59BBD0` 只压了 ebx/esi/edi，ebp 由帧指针序列保住），
所以跨调用也不会被冲掉。

看上去像是漏写了一次回写：本意多半是"围着上一块的落点继续长"，
实际效果是"每次都从离地图正中最近的空格重新起跳"。**照抄**。

#### 一个都挑不出来时起点是 (0, 0)

`0x59B149` 先把候选清成 0，扫完没人满足条件就保持 0。
`(0, 0)` 正好是 `0x59BBC0` 判定"起点为空"的哨兵，于是那一轮改用随机起点。

`WaterAmount` 越大目标越**小**——与"陆地是画出来的"一层反转自洽（画得越少水越多）：

| `WaterAmount` | 目标覆盖率 |
|---|---|
| 0 | 0.500 |
| 50 | 0.475 |
| 100 | 0.450 |

### MapType 2（`FUN_0059B200`）：一刀切两半，每半各自生长

```c
double target = (0.2 - 0.15) * (1.0 - WaterAmount * 0.01) + 0.15;   // 0x7ED748 / 0x7ED740
// 抛硬币决定横切还是竖切
if ((double)(uint32)Random() * (1/4294967295.0) >= 0.5) {
    half[0] = { bx, by,          bw, bh/2 - 1 };     // 上半
    half[1] = { bx, by + bh/2+1, bw, bh/2 - 1 };     // 下半  ← 记法见下
} else {
    half[0] = { bx,          by, bw/2 - 1, bh };     // 左半
    half[1] = { bx + bw/2+1, by, bw/2 - 1, bh };     // 右半
}
for (Rect& rc : half) {          // 两半各跑一遍，共用 iter 计数（上限 100）
    Cell   centre = IsoCentreOf(rc);          // 与 MapType 0 同一条换算式
    Cell   start  = centre;
    int    painted = 0;  double ratio = 0.0;  // 这两个每半重置
    double aspectX, aspectY;                  // 见下
    double ea = 1.0 / (rc.w*0.5 * rc.w*0.5);  // 内切椭圆的两个系数
    double eb = 1.0 / (rc.h*0.5 * rc.h*0.5);
    while (target > 0.0 && iter < 100) {
        int budget = min((int)((target - ratio) * total), cap);
        painted += Paint(budget, &rc, &start, 1, &centre, 0.75, 1);
        ++iter;  ratio = painted / total;
        // 下一起点：线性扫**整个工作数组**（side² 条），
        // 先用等距包围盒粗筛，再过 InRegion（内切椭圆），最后取加权曼哈顿最小者
        start = argmin(...);
        if (ratio >= target) break;
    }
}
```

#### 目标点同样焊死

和 MapType 1 一模一样：`&centre` 是这一半的中心，整轮不变；变的只有 `&start`。

#### ⚠️ `iter` 是两半**共用**的

`0x59B22E` 在外层两半循环**之前**把它清零，循环体里只重置 `painted` 与 `ratio`。
所以上限 100 是两半合起来的：第一半若耗满，第二半一次都跑不了。

#### 等距包围盒的四条边（`0x59B54A`-`0x59B585`）

```c
loSum = W + 2*rc.y + 1;      hiSum = loSum + 2*rc.h + 2;   // 判 X+Y
loDif = 2*rc.x - W + 1;      hiDif = loDif + 2*rc.w + 2;   // 判 X-Y
```

坐标取自工作记录自己的 `+0x00`/`+0x02`（阶段 1 写进去的打包坐标）。
菱形外的记录从没被写过，读出来是 `(0,0)`，`X+Y = 0 < loSum` 直接被刷掉——
**不需要额外的有效性判断**，这也是原版敢直接线性扫 `side²` 条的原因。

#### 各向异性系数（`0x59B411`-`0x59B46D`）

```c
if (rc.h > rc.w) { aspectX = (double)rc.h / rc.w * 1.2;  aspectY = 1.0; }
else             { aspectX = 1.0;  aspectY = (double)rc.w / rc.h * 1.2; }
cost = _ftol(|Y - centre.Y| * aspectY + |X - centre.X| * aspectX);
```

沿长轴的位移权重是 1.0，垂直长轴的位移被放大 `1.2 × 长/短`——
效果是让下一块沿着这一半的长边接着铺。

两点差异值得注意：

1. **目标覆盖率降到 0.15~0.20**——因为是"每半各占这么多"，且两半的
   `painted/total` 分母仍是**全图**可用格数，合起来仍是 0.3~0.4 量级；
2. 选下一中心时扫的是**工作数组**（`side²` 项）而不是地图格子迭代器，
   先用等距坐标做包围盒粗筛，再过椭圆判定，最后取加权曼哈顿最小者。

各向异性系数与涂块核心里的一致（`0x7E5190 = 1.2`）。

### 复刻状态：三种策略都已落成并对拍（✅ 2026-08-06）

`code/rewrite/rmg/place_blobs.{h,cpp}`，黄金基准
`memory/data/rmg/place_blobs_golden.txt`（`dump_place_blobs_golden.py`，
12 组用例 = 三种 MapType × 四种尺寸/人数/水量组合）。
比对三样：**跨所有涂块调用的整张地图陆地生长顺序**、随机数消耗量、收尾块编号。
桩的口径与涂块基准一致（`IsPaintable` 恒真、岸线平滑恒真），
全图边界 `[0xABE2F0]` 给 `(-1,-1,-1,-1)`，好让 `InRegion` 与 `DirectionalCost`
都走真实分支而不是"区域等于全图"的快捷路径。

## 三、区域网格 `FUN_0059A8F0`——把地图切成 n 个投放区

MapType 0 用它把可用区域切成 `n` 个矩形，每个矩形里画一块陆地。

```c
void MakeGrid(vector<Rect>* out, int n, const Rect* bounds) {
    int  r    = (int)FastSqrt((double)n);            // 查表 sqrt 后 _ftol（RC=chop 截断）
    int  cols = (r * r == n) ? r : r + 1;
    int  rows = (n <= cols * r) ? r : cols;          // rows * cols >= n

    // 抛硬币决定网格的行列朝向（等价于把网格转置）
    int cellW, cellH, innerStepX, innerStepY, outerStepX, outerStepY;
    if ((double)(uint32)Random() * (1/4294967295.0) >= 0.5) {
        cellW = bounds->w / cols;  cellH = bounds->h / rows;
        innerStepX = cellW; innerStepY = 0;          // 内层沿 X 走
        outerStepX = 0;     outerStepY = cellH;
    } else {
        cellW = bounds->w / rows;  cellH = bounds->h / cols;
        innerStepX = 0;     innerStepY = cellH;      // 内层沿 Y 走
        outerStepX = cellW; outerStepY = 0;
    }

    // rows*cols 通常比 n 多出 rem 个，随机挑 rem 个外层行「少放一个」
    vector<int> counts(rows, cols), pool(rows);  iota(pool);
    for (int rem = rows * cols - n; rem > 0; --rem) {
        int j = PickAtMost(pool.size() - 1);         // 拒绝采样，不放回
        counts[pool[j]] = r;                         // 该行由 cols 降为 r
        pool.erase(pool.begin() + j);
    }

    int x = bounds->x, y = bounds->y;
    for (int i = 0; i < rows; ++i) {
        if (counts[i] < cols) {                      // 少放一个 -> 居中偏半格
            if (innerStepX > 0) x += cellW / 2; else y += cellH / 2;
        }
        for (int k = 0; k < counts[i]; ++k) {
            out->push_back({ x + 2, y + 2, cellW - 4, cellH - 4 });   // 四周内缩 2
            x += innerStepX;  y += innerStepY;
        }
        x += outerStepX;  y += outerStepY;
        if (innerStepX > 0) x = bounds->x; else y = bounds->y;        // 重置内层轴
    }
}
```

产出的记录数**恰好是 `n`**：只有 `r² != n` 时才有 `rem > 0`，
而那时必有 `cols == r + 1`，于是 `rows*cols - rem*(cols - r) == n` 成立。

### ⚠️ `(int)FastSqrt(n)` 在 n ≥ 16641 时会少 1

查表 sqrt 只有 14 位精度且**系统性偏小**，`trunc` 会把误差放大成整数差。
实测 `n ∈ [0, 100000]` 中有 408 个 `n` 使 `(int)FastSqrt(n) != isqrt(n)`，
最小的是 `16641 = 129²`。

这里 `n = NumPlayers + r ≤ 8 + 4 = 12`，**远在安全区内**，
但同一个「`FastSqrt` 后立刻 `_ftol`」的写法若出现在别处，必须重新核算量级。
复刻时照抄查表实现即可自动继承这个行为，**不要**换成 `std::sqrt`。

### 两处容易照错的宽度与舍入

1. **两个尺寸先都按 `cols` 除**（`0x59A956`），抛硬币之后才有一个改成按 `rows` 除。
   直接写成 `cellW = w/cols, cellH = h/rows` 在 `rows == cols` 时看不出区别，
   `rows != cols` 时就错。
2. **「少放一个」的半格偏移是向零取整的除以 2**（`0x59AB98` `cdq/sub/sar`），
   不是算术右移。`cellW` 为负时两者差 1。

抽行用的是**上限不折叠**的那一路拒绝采样（`0x59AB13`，两次乘法），
与涂块随机起点同款，跟 `PickBelow` 那个折叠版不是一个东西——见
`scaled_random.h` 里 `PickBelowMul` 的注释。

**复刻状态**：`code/rewrite/rmg/make_grid.{h,cpp}`，
黄金基准 `memory/data/rmg/make_grid_golden.txt`
（`code/randmap/dump_make_grid_golden.py`，16 组用例覆盖 `n ∈ [1,25]`、
四种边界矩形、两种硬币朝向），比对**整条矩形序列 + 随机数取数次数**；
另有一条性质测试钉住"产出恒等于 n"（`n = 1..64` 全过）。

## 四、涂块核心 `FUN_0059BBC0`——陆地形状的全部来源

**已完整还原**。本质是一次 **Dijkstra 式的最小堆扩散**：从一个种子格出发，
每次取出"优先级最小"的候选格把它变成陆地，再把它的四个邻格塞回堆里。
优先级 = `FastSqrt(到引力中心的距离)` + 随机抖动，而**引力中心自己在做布朗运动**
——不规则的海岸线就是这么来的。

### 参数

栈上一共 8 个 dword（`ret 0x20`）。**注意起点和目标点的宽度不一样**：

| 位置 | 含义 | 宽度 | 已观察到的取值 |
|------|------|------|----------------|
| `ecx` | `MapSeedClass` | | `0xABDFD8` |
| `[ebp+0x08]` | 预算（要涂多少格） | int32 | 小于 401 一律提到 400 |
| `[ebp+0x0C]` | 区域 `int[4] {bx, by, bw, bh}`（真实坐标） | 指针 | 由投放策略给出 |
| `[ebp+0x10]` | 起点格；两分量都为 0 时随机取 | **两个 int32** | |
| `[ebp+0x14]` | 区域形状：0=矩形，非 0=内切椭圆 | int32 | |
| `[ebp+0x18]` | 方向性代价的目标点 | **打包 int16** | 仅优先级模式 ≠0 时用 |
| `[ebp+0x1C]` | double 中心游走步长 | double（占 2 dword） | MapType 0 用 `0.25`，MapType 1 用 `0.75` |
| `[ebp+0x24]` | 优先级模式：0=距离+抖动，非 0=方向性代价 | int32 | |

> ⚠️ **起点是 `int[2]`，目标点是一个打包成 dword 的 `int16[2]`。**
> 起点在 `0x59BC72` / `0x59BC7C` 用 `mov eax,[ebp+0x10]` / `mov ecx,[ebp+0x14-4]`
> 分两个整字读；目标点在 `0x59B989` 用 `movsx …, word ptr [eax+2]` 取高半字。
> 复刻侧若把起点也按 int16 打包，`y` 会读成 0、起点落到菱形外，整块只涂 1 格。
> 这是本项目实际踩过的坑（对拍 harness 首版即栽在此处）。

返回涂成功的格数；整块被否决时返回 0。

### 主循环

```c
if (budget < 401) budget = 400;
poolCap = max(budget * 8 + 2, 100);
nodes = new Node[poolCap];        // Node { int16 x, y; float priority; }  共 8 字节
heap  = new MinHeap(poolCap);     // 1 基二叉最小堆，比较 *(float*)(node+4)

// 长宽比各向异性系数（常数 0x7E5190 = 1.2），存成 float
if (region 就是全图边界)      costA = costB = 0.0f;             // ← 快捷路径
else if (bw > bh)             costA = (bw/bh)*1.2f, costB = 1.0f;
else                          costA = 1.0f,         costB = (bh/bw)*1.2f;
ellipseA = 1/(bw*0.5)^2 ;  ellipseB = 1/(bh*0.5)^2;      // 0x7E1738 = 0.5，算于 0x59BF9B

double cx = start.x, cy = start.y;        // 引力中心（浮点，会漂移）
work[start].queued = 块编号;
push(start, 0.0f);
painted = 0;

n = pop();                                        // ← 循环外先弹一个
while (painted < budget && n 有效) {
    work[n].owner = MapSeedClass[0x308];          // 块编号
    MapAt(n)->IsoTileTypeIndex = 0;               // ← 这一格由水变陆

    for (int d = 0; d < 8; d += 2) {              // 只取 0/2/4/6 四个基本方向
        p = n + AdjacentCellOffset[d];
        if (p 越出菱形地图边界) continue;          // [0xABED04] / [0xABED08]
        if (work[p].owner != 0) continue;         // 已是陆地
        if (work[p].queued == 块编号) continue;   // 已在堆里
        if (!IsPaintable(MapAt(p))) continue;     // 0x4865D0
        if (已用节点数 >= poolCap) continue;
        if (!InRegion(p, region, 形状, ellipseA, ellipseB)) continue;   // 0x59BAB0

        icx = (int)(cx + 0.5);  icy = (int)(cy + 0.5);   // fadd 0.5 后 _ftol（RC=chop）
        if (优先级模式 == 0) {
            int d2 = (p.x-icx)*(p.x-icx) + (p.y-icy)*(p.y-icy);
            pr = FastSqrt(d2) + (double)(uint32)Random() * 5.0 * (1.0/4294967295.0);
        } else {
            pr = DirectionalCost(p, n, region, 目标点, costB, costA);    // 0x59B940
        }
        work[p].queued = 块编号;                  // ← 先写 stamp
        push(p, (float)pr);                       // 注意：存成 float，不是 double
    }

    cx = Normal() * 步长 + cx;                    // ← 引力中心做布朗运动
    cy = Normal() * 步长 + cy;
    painted++;
    n = pop();                                    // ← 弹在循环体末尾
}
```

#### 起点为 `(0,0)` 时怎么随机取

`0x59BC72` 起：若起点两个 int32 都是 0，就循环抽点，**最多 200 次**：

```c
attempts = 0;
do {
    rx = PickBelow(bw);            // 见下
    ry = PickBelow(bh);
    X = rx + ry + 1;               // 真实坐标 → 等距坐标，int16 打包加法
    Y = ry - rx + bw;
    if (++attempts >= 0xC8) return 0;      // ← 先自增再判断
} while (X/Y 不在菱形内 或 !IsPaintable);
```

其中 `PickBelow(limit)` 是**两次乘法**，不是折叠成一个常数的那种：

```c
u = (double)(uint32)Random() * (double)limit * 2.3283064370807974e-10;   // 0x7ED898
v = _ftol(u);                                    // RC=chop
if ((unsigned)v > (unsigned)(limit-1)) 重抽;      // 无符号比较的拒绝采样
```

> ⚠️ 这**不同于** `scaled_random.h` 里那个 `PickBelow`（后者把 `limit` 和倒数
> 折进了一次乘法）。两次乘法与一次乘法在 1 ulp 上会分岔，必须分开实现。
>
> ⚠️ `++attempts >= 0xC8` 是**自增在前**，所以第 200 次抽签仍然会消耗随机数，
> 只是结果被丢弃、函数直接返回 0。账本上这一路是 400 次取数。

#### ⚠️ 方向性代价的两个系数是**反着传**的

`costA` / `costB` 按上面的规则算出来，调用点却传成
`DirectionalCost(p, n, region, target, costB, costA)`——
即先算出来的那个反而落在后一个形参上。照抄即可，不要"顺手理顺"。

#### ⚠️ 最后弹出的那一格被白白丢掉
因为 `pop()` 在循环体**末尾**、条件判断在**开头**，当 `painted` 恰好撞到 `budget`
时，刚弹出的 `n` 既没有被涂、也已经离开了堆——收尾的排空循环也看不见它。
每次调用都会有恰好一格遭此待遇（除非堆先空了）。这不是等价变形能省掉的：
它让后续的排空循环少处理一个节点，进而改变返回值。

#### ⚠️ `queued` 标记写在入堆之前

`work[p].queued = 块编号` 在 `0x59C233`，push 在其后。于是当节点池已满、
push 实际失败时，这一格**仍然留下了 stamp**，此后再也不会被任何邻格重新考虑。
（不过池满这一路在正常预算下不会走到：`poolCap = budget*8+2`。）

抖动项 `Random() * 5.0 * (1/4294967295.0)` 即均匀 `[0, 5)`——
相当于给"到中心的距离"加上最多 5 格的噪声，边界因此变毛糙而不是圆滑。
乘法顺序（先 ×5 再 ×倒数）已按汇编照抄，换序会在 1 ulp 上分岔。

### 收尾与整块回滚

```c
while ((n = pop()) 有效) {           // 预算用尽，把队列里剩下的边缘格也铺成陆地
    if (work[n].owner == 0 && IsPaintable(MapAt(n))) {
        MapAt(n)->IsoTileTypeIndex = 0;
        work[n].owner = 块编号;
    }
    painted++;                       // ← 无条件自增，即使上面那格没涂成
}
free(nodes); free(heap 数组); free(heap);

if (!FUN_0057A0C0(块编号, 1)) {       // 铺岸线过渡砖；铺不出来就否决整块
    for (每个 owner == 块编号 的格子) {
        work[..].owner = 0;  work[..].+0x4B = 0;
        cell->IsoTileTypeIndex = [0xAA0738];      // 还原成水
        cell[+0x11A] = 0;  cell[+0x11B] = MapSeedClass[0x30C];
    }
    return 0;                        // ← 调用方据此重试（MapType 0 最多试 10 次）
}
MapSeedClass[0x308]++;               // ← 块编号推进到下一块
return painted;
```

#### ⚠️ 排空循环里的 `painted++` 是无条件的

它数的是"弹出了几个节点"，不是"涂成了几格"。而 `painted` 就是**返回值**。
于是返回值 ≥ 实际涂成的格数：堆里那些"已经是陆地/不可涂"的残留节点
也会被计进去。投放策略拿这个返回值去累计"还差多少格陆地"，
所以这个偏差会一路传导到整张地图的陆地总量上——必须照抄。

（对拍数据里 `ret` 与 `cells` 恰好相等，是因为测试桩的 `IsPaintable` 恒真、
残留节点全都还没被涂过。真实 theater 下两者会分开。）

#### ⚠️ 平滑成功后还有一次**无条件**的岸砖清扫

在 `FUN_0057A0C0` 返回真之后、`[0x308]++` 之前，函数还会再遍历一次全图，
把所有 `IsoTileTypeIndex ∈ [ShorePieces, ShorePieces+0x2A)` 的格子
**统统改回 tile 0** 并把 `+0x11A` 清零。它不看块编号、不看这一块涂到了哪里——
整张地图上一切岸线砖都被抹平，留待下一块（或后续阶段）重铺。
这解释了为什么多次调用涂块之后岸线仍然是一致的：每一块都会把前人的岸砖推倒重来。

`FUN_0057A0C0` 不只是校验：它遍历全图跑四遍平滑（`0x57A430` / `0x57A320` /
`0x57ACF0(...,1)` / `0x57ACF0(...,2)`），顺带在工作数组缺席时把它建出来。
**任一遍返回失败，整块陆地就被抹回海里**——这是"地图有时大片是海"的直接机制。

> `MapSeedClass +0x308` 至此可以定名了：它就是**块编号（生成戳）计数器**。
> 同一次涂块内它当作 `work[].owner` 和 `work[].queued` 的标记值，
> 涂块成功才自增、失败则原地不动（于是下一次重试复用同一个编号）。
> 早前文档里把它标为「未确认」，现已关闭。

### 最小堆本身：堆序就是生长顺序（✅ 已实锤并对拍）

上面写"最小堆"三个字是不够的——**并列时谁先出堆**直接决定哪一格先变陆地。
所以这部分逐条抄了汇编。

堆对象 `malloc(0x14)`（`0x59BC21`）：

| 偏移 | 含义 |
|------|------|
| `+0x00` | 当前元素数 `count` |
| `+0x04` | 容量 `cap`（= `poolCap`） |
| `+0x08` | `PaintNode*` 数组，`malloc(cap*4 + 4)`，**1 基** |
| `+0x0C` / `+0x10` | 入堆过的最大 / 最小节点**指针**，纯统计，无人读取 |

建好后把 `arr[0..cap-1]` 清零（`0x59BC5C`），**`arr[cap]` 不初始化**。

- **下沉 `0x5AD870`**（唯一没被内联的一段，`__thiscall(this, int i)`）：

  ```c
  best = (a[i] > a[2i])   ? 2i   : i;      // 0x5AD881
  if   (a[best] > a[2i+1])  best = 2i+1;   // 0x5AD89F
  ```

  两处都是 `fcompp` + `test ah,0x41`，即**严格大于**——**相等一律不交换**。

- **上浮**（内联在 `0x59C23C`）：`while (i > 1 && a[i>>1] > pr) { a[i] = a[i>>1]; i >>= 1; }`，
  同样是相等即停。

- **弹出**（内联在 `0x59BF75`、`0x59C2F9`、`0x59C33C`、`0x59C3C5` 共四处）：
  取 `arr[1]`，把 `arr[count]` 搬到根，末槽清零，`count--`，再 `SiftDown(1)`。

#### ⚠️ 两条必须照抄的怪癖

1. **最后一格永远用不上。** 入堆门槛写的是 `count + 1 >= cap`（`0x59C250 cmp ecx,edi / jae`），
   于是实际容量是 `cap - 1`。
2. **堆满时静默丢弃。** 越界那条 `jae` 直接跳过整个插入，
   但此时节点已经从池里分配掉、`work[p].queued` 也已经写成了当前块编号——
   也就是说这个候选格**再也不会被考虑**，而调用方毫不知情。
   这是"陆地偶尔莫名其妙缺一角"的候选解释之一（**是否真能观察到未确认**，
   `poolCap = max(budget*8+2, 100)` 相当宽裕）。

顺带实锤了工作数组的寻址（`0x59C377`-`0x59C393`）：

```asm
0059c37d  movsx eax, word ptr [edi + 2]     ; node.Y
0059c381  imul eax, dword ptr [0x89c2dc]    ; × side
0059c388  movsx edx, word ptr [edi]         ; node.X
0059c38b  add eax, edx
0059c38d  lea edi, [eax + eax*4]            ; ×5
0059c390  shl edi, 4                        ; ×16  → ×0x50
```

即 `work[(Y*side + X) * 0x50]`，且节点里的坐标是 `int16`（`movsx` 读回）。

**复刻状态**：`code/rewrite/rmg/blob_heap.h`，回归 `test/test_blob_heap.cpp`。
下沉是 Unicorn 直接跑 `0x5AD870` 取的黄金值（48 例，一半刻意制造并列），
存档 `memory/data/rmg/blob_heap_golden.txt`；
上浮与弹出因为**内联在涂块函数里、没法单独调用**，只能逐条抄汇编 +
结构不变量兜底，取证强度低一档，**如实标注**。

### 邻接方向表 `0x89F688`（初始化于 `0x49F2F0`）

`.bss` 里的 8 项 `{int16 dx, int16 dy}`，运行期由 `0x49F2F0` 写入：

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| (dx,dy) | (0,-1) | (1,-1) | (1,0) | (1,1) | (0,1) | (-1,1) | (-1,0) | (-1,-1) |

涂块用步长 2 取 `0/2/4/6` → `(0,-1) (1,0) (0,1) (-1,0)`。
这是**等距坐标**下的四个基本方向，对应真实坐标里的四个对角邻格。

### 区域判定 `FUN_0059BAB0`（✅ 已实锤并对拍）

```c
if (region 就是全图边界 [0xABE2F0]) return true;
if (形状 == 0)                          // 矩形：直接拿等距坐标 (X,Y) 比
    return bx <= p.x && p.x < bx+bw && by <= p.y && p.y < by+bh;
// 椭圆：先做等距逆变换  2rx = p.x - p.y + W - 1 ，2ry = p.x + p.y - W - 1
dx = (rx - bx) - bw/2;
dy = (ry - by) - bh/2;
return dx*dx*ellipseA + dy*dy*ellipseB < 1.0;
   ⇔ ((rx - (bx+bw/2)) / (bw/2))² + ((ry - (by+bh/2)) / (bh/2))² < 1
```

即**矩形区域的内切椭圆**；系数里的 `0.5` 在两边完全约掉，只是防溢出的缩放。

注意矩形分支拿的是**等距**坐标、椭圆分支拿的是**真实**坐标——两条分支口径不一致。
模拟执行 600 例（矩形 / 椭圆各半）**逐例确认行为确实如此**，
所以这不是我读错汇编；但**它是有意还是口误，仍然未确认**，照抄。

三处细节也一并钉死：
- 全图边界那条快捷路径是**四个字段逐一比较**（`0x59BABD` 起），不是指针相等
- 矩形分支是半开区间 `[x, x+w) × [y, y+h)`
- 椭圆判据是**严格小于** `1.0`（`0x59BB68 fcomp` + `test ah,1`）
- 原版先把 `2*rx`、`2*ry` 凑成整数再 `fild`、再乘 `0.5`（`[0x7E1738]`），
  复刻保持同一顺序，不要直接算 `rx`

**复刻状态**：`code/rewrite/rmg/blob_region.{h,cpp}`，120 例黄金对拍。

### 方向性代价 `FUN_0059B940`（优先级模式 ≠ 0，✅ 已实锤并**逐位**对拍）

```c
if (region 就是全图边界) return FastSqrt(dy² + dx²);     // 退化成欧氏距离
ax_ = |Δrx(n → 目标点)| ;  ay_ = |Δry(n → 目标点)|;      // 由 (x∓y)>>1 得到
if (ax_ == 0 && ay_ == 0) return 0.0;
len  = FastSqrt(ay_² + ax_²);
cheb = max(|p.y - n.y|, |p.x - n.x|);                    // 切比雪夫步距
return cheb * (aspectX * (ax_/len) + (ay_/len) * aspectY);
```

即：**步距按"朝目标点的方向"做各向异性加权**，让这块陆地朝指定方向拉长。

参数是 6 个 dword（`ret 0x18`）：`(p, n, region, target, float aspectX, float aspectY)`。
`ax_`/`ay_` 量的是 **n 与 target** 的方向，`cheb` 量的是 **p 与 n** 的步距——两对不一样，别混。

#### 三条必须照抄的细节

1. **方向分量用算术右移 `(X ∓ Y) >> 1`**（`0x59B9A3`/`0x59B9BA` 是 `sar`）。
   负数右移向 −∞ 取整，与"除以 2 向零取整"**不是一回事**，代价值会实打实地变。
2. **`aspectX`/`aspectY` 是 `float` 不是 `double`**（`0x59BA44`/`0x59BA48` 都取 dword），
   加宽发生在乘法之前。
3. **`RC=chop` 在这里是硬约束**：同一批 400 例随机输入，
   模拟器在 `RC=near` 下有 **370 例**与 `RC=chop` 结果不同。
   （顺带一提，这也是验证建模的好办法——把模拟器切到就近舍入，
   我那个用 Python 原生浮点写的模型立刻 400/400 全中，
   反过来证明公式本身没错、差的只是舍入模式。）

**复刻状态**：`code/rewrite/rmg/blob_region.{h,cpp}`，120 例**逐位相等**（不设容差）——
它的结果直接进最小堆当优先级，差 1 ulp 就可能换掉两个候选格的先后。
黄金值存档 `memory/data/rmg/blob_region_golden.txt`。

### 可涂性谓词 `FUN_0004865D0`

只看 `cell->IsoTileTypeIndex`（`+0x38`），落进下列任一区间即可涂：

| 区间 | 长度 |
|------|------|
| `[ [0xABAD28], +0x2A )` | 42 |
| `[ [0xAA0738], +0x0E )` | 14（水面族） |
| `[ [0xAA073C], +4 )` / `[ [0xABB110], +4 )` / `[ [0xAA1050], +4 )` / `[ [0xAA10A0], +4 )` | 各 4 |

这些基址都是运行期由 theater 决定的 TileSet 起始号，**具体对应哪些 TileSet 未确认**。

### 复刻状态：整函数已落成并对拍（✅ 2026-08-06）

`code/rewrite/rmg/blob_paint.{h,cpp}` —— `PaintBlob(PaintContext&, const PaintParams&)`。
地图访问被抽成 `PaintWorld` 抽象接口（`IsPaintable` / `IsoTile` / `SetIsoTile` /
`SmoothShoreline` / `ShorePiecesBase` / `WaterTile`），
这样 theater 相关的 TileSet 基址还没定下来也不妨碍主循环先对拍。

对拍手段比之前几个基元都强一档：**不比标量，比整条生长顺序**。
`code/randmap/dump_blob_paint_golden.py` 用 Unicorn 整函数模拟 `0x59BBC0`，
在 `0x59C02B`（主循环涂格）和 `0x59C3AB`（排空涂格）挂钩子记下每一格的坐标，
同时在 `0x65C780` 挂计数器统计随机数取数次数。
黄金数据 `memory/data/rmg/blob_paint_golden.txt`，14 组用例：

| 覆盖到的 | 用例 |
|----------|------|
| 随机起点 / 指定起点 | 全部 / #2 #5 #11 #13 |
| 矩形区域 / 内切椭圆 | #2 #4 #6 / #3 #11 #12 |
| 全图边界快捷路径 | #1 #2 #6 #7 #8 #13 #14 |
| 优先级模式 0 / 方向性代价 | 多数 / #5 #11 |
| 游走步长 0.25 / 0.75 | 两者都有 |
| 预算下限（60 → 400） | #6 |
| 大预算（3000，涂 3249 格） | #14 |
| 起点落在菱形外 → 直接退化 | #13 |
| 区域太窄，200 次抽签全失败 | #12（`ret=1`, 仅 4 次取数） |
| 地图尺寸 60/70/80/120 | 各有 |

三条测试（`test_blob_paint.cpp`）全绿：

- `GrowthOrderMatchesOriginal` —— 约一万格，**逐格坐标序列完全一致**；
- `RandomDrawCountMatchesOriginal` —— 14 组取数次数逐组吻合
  （这是独立于结果的第二条证据链：顺序对而次数不对，说明有分支白抽了随机数）；
- `BudgetIsFlooredAt400`。

> ⚠️ **一条诚实的保留**：模拟器里 `NormalDeviate` 走的是 Unicorn 的 `fyl2x`，
> 而 Unicorn 的 x87 实现**不理会 FPU 控制字**，所以黄金数据在这一步用的是
> 宿主的 `long double` 精度，不是原版的 PC=53。
> 14 组、约一万格里没有出现过分歧（布朗运动只影响引力中心，
> 而优先级里还叠了一个幅度达 5 格的随机抖动，对中心的亚 ulp 抖动不敏感），
> 但这个口子必须记在案：真要较真，得换成在真机上跑或自己实现 `f2xm1`/`fyl2x`。

## 五、已确认的谓词与全局

| 地址 | 判定 | 依据 |
|------|------|------|
| `0x486380` | `tile == 0 \|\| tile == 0xFFFF`（地块未指定） | 反编译，只读 `+0x38` |
| `0x4865B0` | `tile ∈ [ [0xABAD28], +0x2A )`（某 42 格 tile 组内） | 反编译；**组的语义未确认** |
| `0x4865D0` | 可涂性谓词（tile 属于水/浅滩族） | 已还原，见 §四 |
| `0x87F8DC` / `0x87F8E0` | 地图宽 / 高 | 多处用作随机上限；`FUN_00599650` 用 `W/2+H/2` 算中心 waypoint |
| `0x89C2DC` | 工作数组边长（方阵） | `0x578E60` 按 `side²` 分配 |
| `0xABED10` | 每格 0x50 字节工作记录数组 | `0x578E60` 分配 `side²×0x50` |
| `0xAA0738` | 水面 tile 组起始索引 | 全图初始化与 `FUN_0059C630` 都用它做基准 |
| `0xAA0E18` | 岸边 tile 索引 | `FUN_0059A6C0` 末尾给水陆交界格赋值 |

工作记录已知字段（**其余未确认**）：

| 偏移 | 已观察到的行为 |
|------|----------------|
| `+0x38` | 阶段开始置 `0xFFFFFFFF`；涂块时写入块编号，`== 0` 表示尚未属于任何块 |
| `+0x3C` | 布局前清 0；候选入堆时写入块编号（防重复入堆） |
| `+0x4A` | 数组分配后置 1 |

## 六、水面 tile 细化 `FUN_0059C630`（✅ 已落成 C++ 并对拍）

它由 `0x598B14` **无条件**调用——旁边那个 `0x59C580` 才是 `MapType ∈ {3,4}`
且 `WaterAmount != 0` 的专属支路，两者别混（`0x598AED-0x598B14`）：

```asm
00598aed  mov  eax, [ebp + 0x3c]      ; MapType
00598af3  cmp  eax, 3 / je            ; 3、4 走 0x59C580（还要 +0x4C != 0）
00598afd  call 0x59a6c0               ; 其余走阶段 2 调度器
00598b14  call 0x59c630               ; ← 两条路都要过这里
```

作用是把阶段 2 留下的一整片单调水面换成花砖：能凑出 2×2 的地方压一块
「大水面」，凑不出（或抽中 10% 的那一签）就只给自己换一块「小浪花」。
它是阶段 2 之后**第一个还在消耗随机数**的地方。

```c
// 0x59C630，ret 0；迭代顺序 == Reset 0x578350 / Next 0x578290
void MapSeedClass::RefineWaterTiles() {
  for (CellClass* cell : AllCells()) {
    if (cell->IsoTile != [0xAA0738]) continue;        // 0x59C657 等值比较
    if (cell->SubTile != 0)          continue;        // 0x59C668 字节比较

    // 0x59C674-0x59C68E：三次 AdjacentCell，顺序 2(E) → 4(S) → 3(SE)，
    // **先全取完再判**（取的过程会改写共享哑元的坐标）
    CellClass* e  = cell->Adjacent(2);
    CellClass* s  = cell->Adjacent(4);
    CellClass* se = cell->Adjacent(3);

    bool big = e->IsoTile == [0xAA0738] && e->SubTile == 0        // 0x59C699
            && s->IsoTile == [0xAA0738] && s->SubTile == 0        // 0x59C6B0
            && se->IsoTile== [0xAA0738] && se->SubTile== 0;       // 0x59C6C5

    if (big && PickFrom1(10) != 1) {                  // 0x59C6DA，10% 落空
      int n = PickBelow(242);                         // 0x59C70F
      int shape = (n >= 240) ? (247 - n)              // 0x59C742
                             : (n / 40);              // 0x59C74B 魔数 0x66666667
      int blob = ([0xABED10] == 0) ? -1               // 0x59C767
                 : work[Y * side + X].field_38;       // 0x59C770
      StampTile([0xAA0738] + shape, &cell->MapCoords, blob, -1);  // 0x59C795
      continue;
    }
    int n = PickBelow(201);                           // 0x59C79C
    cell->IsoTile = [0xAA0738] + 8 + n / 40;          // 0x59C7C7 魔数 0xCCCCCCCD
  }
}
```

**两处除法都是 `/ 40`**（早前记的 `/10` 是错的）。魔数拆开算：
`0x66666667 / 2^(32+4) = 1/40`（`imul` + `sar edx,4`，有符号），
`0xCCCCCCCD / 2^(32+5) = 1/40`（`mul` + `shr edx,5`，无符号）。于是——

| 支路 | 抽样 | 砖号 | 说明 |
|---|---|---|---|
| 大水面 | `PickBelow(242)` | `WaterSet + 0..7` | `n ∈ [0,239] → n/40 ∈ [0,5]`；`n = 240 → 7`、`n = 241 → 6`，两者各 1/242 |
| 小浪花 | `PickBelow(201)` | `WaterSet + 8..13` | `n/40 ∈ [0,5]`，六块等概率（末段 200 单独一档，是原版的偏斜） |

`0 + 8 = 8` 到 `13` 正好把 WaterSet 的 `TilesInSet = 0x0E` 用满，
而前 8 块留给 2×2 大水面——**砖号分配与组宽自洽**，是对 `/40` 的独立佐证
（若真是 `/10`，`shape` 会长到 23，越出整组）。

### 压砖器 `FUN_005A6C10`（✅ 已实锤并对拍）

`__fastcall`，`ecx` = 砖在 `[0xA8ED2C]` 里的下标、`edx` = 落点坐标指针，
两个栈参（`ret 8`）：`arg0` = 归属块编号、`arg1` = 基准层高（`-1` = 不写层高）。
全镜像 **49 处**调用点，RMG 主路径上只有 `0x59C795` 一处，其余落在
`0x58F/0x593` 与 `0x5A5` 两棵死代码子树里（§九 TODO）。

```c
// 0x5A6C10-0x5A6D73
void StampTile(int tileIndex, CellCoord* at, int blob, int level) {
  TileTypeClass* tt = TileTypes[tileIndex];
  int* offs = tt->vtbl->GetBlockOffsets();      // 0x5A6C26，虚表 +0x9C
  if (!offs) return;
  for (int y = 0; y < tt->Height; ++y)          // +0x2E8
    for (int x = 0; x < tt->Width; ++x) {       // +0x2E4
      int16 X = at->X + x, Y = at->Y + y;       // 0x5A6C68，两分量各按 int16 加
      if (!InMap(X, Y)) continue;               // 0x5A6C90-0x5A6CB4，四不等式
      CellClass* c = GetCellAt(X, Y);           // 0x5657A0
      int k = tt->Width * y + x;
      TmpBlock* b = offs[k + 4];                // [edx + ecx*4 + 0x10]
      if (!b) continue;                         // 空洞
      c->SubTile = (byte)k;                     // +0x11A
      c->IsoTile = tt->TileNumber;              // +0x38 = tt->+0x294
      if (level != -1)                          // 0x5A6CF7
        c->Level = (byte)(b->Height + level - 4);   // +0x11B，注意 **-4**
      c->Ramp = b->RampType;                    // +0x11C，**无条件**
      if ([0xABED10]) work[Y * side + X].field_38 = blob;   // 0x5A6D3A
    }
}
```

它与贴砖器 `0x57B440`（§七）是**两套东西**，四条差别必须记牢：

1. **一格校验都不做**——不看归属、不看 allow 区间、不看地形保护，压上就是压上；
2. **多写一个字段** `CellClass +0x11C = 子块的 RampType`（`.tmp` 子块头 `+0x2A`）。
   `0x57B440` 从头到尾不碰 `+0x11C`——这是两者唯一可以从产物上区分的写入；
3. **层高公式带一个 `-4`**：`+0x11B = 子块高度 + level - 4`，
   而 `0x57B440` 是 `子块高度 + base_level`，没有 `-4`；
4. **归属无条件写**，没有"抢占/同族"那套判定。

`level == -1` 时层高整条跳过，但 `+0x11C` 照写不误——水面细化走的正是这一路。

### 复刻状态（✅ 2026-08-06）

`code/rewrite/rmg/water_refine.{h,cpp}` + `place_tile.cpp` 的 `StampTile`。
基准 `memory/data/rmg/water_refine_golden.txt` 由
`code/randmap/dump_water_refine_golden.py` 取得：**先把 `0x59A6C0` 整个跑一遍**
再调 `0x59C630`，两段的随机数次数分别记账（`rngstage` 一栏与 stage_water 基准的
`rng` 一栏逐例相同，互为交叉验证）。12 组用例比对每格
`(砖号, 子格号, 层高, **坡度**, 归属)` 与共享哑元的 `+0x38`。

夹具里 WaterSet 的 0x0E 块砖按原版实情合成（前 8 块 2×2、后 6 块 1×1），
子块 `+0x2A` 给了 17 个互不相同的非零值——基准里 1..17 **全部出现**，
证明 `+0x11C` 那条写入确实被压到了。三次扰动（`/40` 改成 `/10`、
第三个邻格方向 3 改成 1、去掉 `+0x11C` 的写入）均如期报错，基准非空转。

⚠️ 砖尺寸仍是合成值，换成真实 `.tmp` 尺寸要重取基准（§九 第 19 条）。

## 七、岸线平滑 `FUN_0057A0C0`——并且**它会消耗随机数**（✅ 整体已复刻并对拍）

涂块收尾时调用它铺水陆过渡砖，返回失败则整块陆地被抹回海里（§四）。
它是 `MapClass` 的方法，`ecx = 0x87F7E8`（`0x57B2F1` 读 `[this+0x13C]` 取格子指针数组）。
结构是**四遍全图扫描**：

```c
bool Shoreline(int blobId, int flag) {                     // 涂块调用时 flag = 1
    if (work == null) { work = new Rec[side*side]; owned = true; }   // 缺席时就地建出来
    for (i : side*side) work[i].field_40 = -1;
    bool ok = true;
    ResetIterator(); while ((cell = Next()) && ok) ok = Pass1(cell, blobId, flag);  // 0x57A430
    ResetIterator(); while ((cell = Next()) && ok)      Pass2(cell, blobId, flag);  // 0x57A320 ← 注意
    ResetIterator(); while ((cell = Next()) && ok) ok = Pass34(cell, 1, blobId, flag);
    ResetIterator(); while ((cell = Next()) && ok) ok = Pass34(cell, 2, blobId, flag);
    if (owned) { free(work); work = null; }
    return ok;
}
```

每遍之前都重设四个全局（`0x87F8F4/F8/FC/900` = `MapClass +0x10C/110/114/118`），
即**每遍都从头扫全图**。

### ⚠️ 第 2 遍的返回值被彻底丢弃

第 1 遍（`0x57A1A6`）和第 3/4 遍（`0x57A25C`/`0x57A2B9`）调用后都有 `mov bl, al`
把结果收进循环条件；**唯独第 2 遍（`0x57A1F7`）后面没有这一句**。

```asm
0057a1f7  call 0x57a320
0057a1fc  mov  ecx, 0x87f7e8      ; ← 直接就去取下一格了，al 没人要
0057a201  call 0x578290           ; Next
```

对照 `0x57A320` 自己：它在 `0x57A423` 的出口**根本没设过 `eax`**——
返回的是上一条指令碰巧留下的值（早退路径是 `cell->+0x38`，
掩码不匹配路径是掩码值，走完全程则是最后一次掩码调用的结果）。
函数写的是 `void`，调用方也当 `void` 用，两边自洽。

> 所以「任一遍失败就整块作废」这句话**对第 2 遍不成立**。
> 第 2 遍循环里那个 `test bl,bl` 是循环不变量，仅仅表示"第 1 遍成功了没有"。
> 本文档早前把四遍一概写成 `ok = pass(...)`，是错的，已更正。

### ⚠️ 第 2 遍的判水区间是 `0x0C`，不是 `0x0E`

`0x57A330` 起：

```asm
0057a330  mov eax, [ebp+0x38]
0057a333  cmp eax, edx            ; edx = [0xAA0738]
0057a335  jl  跳过
0057a33b  add edx, 0xc            ; ← 0x0C
0057a33e  cmp eax, edx
0057a340  jge 跳过
```

而公共谓词 `IsWater`（`0x485060`）用的是 `+0x0E`。也就是说**水面组最后两个下标
被第 2 遍排除在外**。是有意（那两块可能是特殊水砖）还是笔误无从判断，照抄。


### ⚠️ 第 3/4 遍每格无条件抽一个随机数

`FUN_0057ACF0` 的**第一条有效指令**就是取数，在任何判断之前：

```asm
0057acf9  mov edx, 5          ; hi = 5
0057acfe  xor ecx, ecx        ; lo = 0
0057ad04  call 0x598030       ; PickRange(0, 5)
```

结果只用到最低位（`r & 1`）做二选一的贴砖变体。也就是说：

> **第 3 遍和第 4 遍各对全图每一格消耗一次 `Random()`。**

这是本阶段随机数消耗的**大头**，量级是 `2 × 扫到的格子数 × 涂块次数`，
而不是"每块地几十次"。漏掉它，随机数流会在第一块陆地之后就彻底错位。

### `FUN_00598030` = `PickRange(lo, hi)`（闭区间，拒绝采样）

```c
int PickRange(int lo, int hi) {          // __fastcall: ecx = lo, edx = hi
    double span = (double)(uint32)(hi - lo + 1);   // ← 注意 uint32
    double base = (double)(uint32)lo;              // ← 注意 uint32
    int r;
    do {
        r = (int)((double)(uint32)Random() * span * (1.0/4294967295.0) + base);
    } while ((unsigned)r > (unsigned)hi);          // 0x598089 是 ja，无符号
    return r;
}
```

两处宽度必须照抄：`0x59803E`/`0x59804A` 把 `span` 和 `lo` 写进栈上 qword 的
**低半、高半置 0** 再 `fild qword`，所以两者都是按 `uint32` 读的——
`lo` 为负会变成天文数字。已知调用点都是 `lo >= 0`，但不要替它"修正"。
拒绝判定 `0x598089` 是 `ja`（无符号），不是 `jg`。

乘法顺序（先 `× span` 再 `× 倒数` 最后 `+ base`）已按汇编照抄，换序会在 1 ulp 上分岔。
以 `(0,5)` 调用时被拒绝的概率约 `2⁻³²`，但**拒绝分支必须保留**——
一旦命中就会多吃一个随机数。

> 注意：`0x598030` 与正态分布 `0x5980C0` **只是地址相邻，毫无关系**。
> 早先的反编译脚本把它标成"0x5980C0 的下层"，是错的，已更正。

### 核心工具 `FUN_0057B210` = 八邻域「水」掩码（已实锤）

签名是 `__thiscall MapClass::CellMask(this, CellClass* cell, int mode)`
（`0x57B22B mov ecx, ebp` 把 cell 传给谓词，`[esp+0x1c]` 是第一个栈参）。

**一个 bit = 一个邻格是不是水砖**。判水用 `FUN_00485060`：

```c
bool IsWater(cell) { return base <= cell->IsoTileTypeIndex
                         && cell->IsoTileTypeIndex < base + 0x0E; }   // base = [0xAA0738]
```

取邻格靠指针算术 `p = cellPtrArray + (Y*0x200 + X) - 0x201`，即 p 指向左上角，
八个偏移与 bit 的对应关系由此**唯一确定**（`p[0x201]` 是自己，被跳过）：

| 偏移 | 相对坐标 | bit |
|------|----------|-----|
| `p[0]` | (X−1, Y−1) | `0x40` |
| `p[1]` | (X&nbsp;&nbsp;, Y−1) | `0x80` |
| `p[2]` | (X+1, Y−1) | `0x01` |
| `p[0x200]` | (X−1, Y&nbsp;&nbsp;) | `0x20` |
| `p[0x202]` | (X+1, Y&nbsp;&nbsp;) | `0x02` |
| `p[0x400]` | (X−1, Y+1) | `0x10` |
| `p[0x401]` | (X&nbsp;&nbsp;, Y+1) | `0x08` |
| `p[0x402]` | (X+1, Y+1) | `0x04` |

按 bit 序号 0→7 走一圈正好是 (+1,−1) → (+1,0) → (+1,+1) → (0,+1) → (−1,+1)
→ (−1,0) → (−1,−1) → (0,−1)，是个规规矩矩的环形编号。

对照 `CellClass::GetNeighbourCell`（`0x481810`）用的八方向增量表 `0x89F688`
——它在 `.bss` 里，由 `0x49F2F0` 运行期填入，实际值逐条读汇编得到：

| dir | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| 增量 | (0,−1) | (+1,−1) | (+1,0) | (+1,+1) | (0,+1) | (−1,+1) | (−1,0) | (−1,−1) |
| 方位 | N | NE | E | SE | S | SW | W | NW |

于是位定义可以压成一句话：

> **`mask` 的 bit k ⟺ `AdjacentCell[(k + 1) & 7]` 是水砖。**

（bit0=NE、bit1=E、bit2=SE、bit3=S、bit4=SW、bit5=W、bit6=NW、bit7=N。）

**边界不产生假岸线**：邻格指针为 0（出图）时，该位取**中心格自己**的 `IsWater`，
所以地图边缘不会因为"外面没水"而被判成岸。

三个 mode 只差一道**前置门槛**，掩码算法本身完全相同：

| mode | 门槛 |
|------|------|
| 0 | 需 `FUN_00486380(cell)`：`IsoTileTypeIndex` 必须是 `0` 或 `0xFFFF`（尚未定砖） |
| 1 | 中心格自己是水砖则直接返回 0 |
| 2 | 无门槛 |

另外两道公共门槛：出了菱形边界（`0xABED04/08`）返回 0；工作记录 `+0x4A` 为 0 返回 0。

> ⚠️ **掩码有缓存，而且缓存不区分 mode**。函数先读工作记录 `+0x40`，
> **只有它为负**才现算；现算结果**不写回**（写回由调用方做）。
> 于是 `0x57A430` 里那对相邻调用
> `m2 = mask(cell,2); rec->+0x40 = m2; m0 = mask(cell,0);`
> 第二次读到的其实是刚写进去的 `m2`——**`m0` 恒等于 `m2`**。
> 是有意为之还是原作者的疏漏无从判断，但复刻必须照抄这个行为，
> 否则第 1 遍的判定会整体走样。各遍开头把全部 `+0x40` 置 `-1` 就是在清这个缓存。

求值顺序也已逐条对上（这决定了各种短路的先后）：

1. `water = [0xAA0738]`；`selfIsWater = IsWater(cell)`
   ——**在 mode 门槛之前就算好**，出图的邻格靠它顶位；
2. 菱形四不等式（`0xABED04`/`0xABED08`），出界返回 0；
3. mode 门槛（上表）；
4. 工作记录 `+0x4A` 为 0 返回 0；
5. 缓存 `+0x40 >= 0` 直接返回；
6. 才轮到八邻域扫描。

每个邻格的判据是 `bit = n ? IsWater(n) : selfIsWater`
（原版写成 `add esi,4` 之后再 `cmp [esi],0`，两者等价——
指针已经前移到刚读过的那一槽）。

**复刻状态**：`code/rewrite/rmg/shore_mask.{h,cpp}`，
黄金基准 `memory/data/rmg/shore_mask_golden.txt`
（`code/randmap/dump_shore_mask_golden.py`，两张随机地图 × 全部格子 × 三种 mode
= **1299 次查询、122 个不同掩码值**），外加位定义、边界继承、缓存越权、
mode 门槛、无效记录五组单独的行为测试。本函数不碰浮点也不取随机数，
所以没有 `fyl2x` 那类保留。

### 四遍各自在做什么

| 遍 | 地址 | 行为 |
|----|------|------|
| 1 | `0x57A430` | **侵蚀**：识别贴不出砖的形状，把该格退回水，并递归传播到八邻 |
| 2 | `0x57A320` | **清理**：把仍是水砖、且掩码落在 8 个"坏值"里的格子标成 `0xFFFF`，并让八邻的掩码缓存失效 |
| 3 | `0x57ACF0(…, 1)` | 先抽 `PickRange(0,5)`；掩码用 **mode 0**；直边 + 对角砖 |
| 4 | `0x57ACF0(…, 2)` | 先抽 `PickRange(0,5)`；掩码用 **mode 1**；四种直角拐角砖 |

`0x57AD0F` 那句 `cmp edi,2 / jne` 决定传给掩码的 mode：
**第 3 遍传 0、第 4 遍传 1**，`mode 2` 只有第 1、2 遍用。

**第 1 遍侵蚀的触发条件**（`mask` 用上面的位定义读）：

```c
mask == 0x0B || mask == 0x1A                               // 两个特例
|| (mask & 0xA0)==0xA0 && (mask & 0x11)==0                 // 西+北是水，而东北/西南是陆
|| (mask & 0x82)==0x82 && (mask & 0x44)==0                 // 北+东
|| (mask & 0x0A)==0x0A && (mask & 0x11)==0                 // 东+南
|| (mask & 0x28)==0x28 && (mask & 0x44)==0                 // 南+西
|| (mask & 0x88)==0x88                                     // 上下都是水 → 一格宽的横向地峡
|| (mask & 0x22)==0x22                                     // 左右都是水 → 一格宽的纵向地峡
|| 「隔一格 / 隔两格的对侧也是水」                            // 2~3 格宽的地峡，见下
```

最后那条是四段结构一样的探测，以 `bit 0x20`（西边是水）为例：

```c
if (mask & 0x20)                                  // 西边是水
    if (mask(cell + (1,0)) & 0x02 || mask(cell + (2,0)) & 0x02)   // 东边一格或两格外，其东侧也是水
        erode = true;
```

另外三段分别是 `0x02`→向 −X 探 `0x20`、`0x08`→向 −Y 探 `0x80`、`0x80`→向 +Y 探 `0x08`，完全对称。
配上前面的 `0x22 / 0x88`，效果就是**宽度 1~3 格的陆桥一律铲掉**——
等距贴图集里没有这么窄的陆地可画。注意它**不检查中间那几格是不是陆地**，
只看两侧的水，所以偶尔会误伤；照抄即可，不要"修正"。

触发之后：

```c
int owner = work[Y*side + X].blobId;              // 0x5A00C0
if (owner > 0 && owner != curBlob && !flag) return 0;   // 撞到别的陆块 → 整块作废
cell->IsoTileTypeIndex = [0xAA0738];              // 退回水
work[...].blobId = flag ? 0 : curBlob;            // 0x5A0090
for (d = 0; d < 8; ++d) { 邻格在界内 → 清其掩码缓存为 -1，递归 }
```

`FUN_005A00C0/0090` 一读一写，坐标算式都是 `(Y*side + X)*0x50 + 0x38 + [0xABED10]`，
**实锤了工作记录数组按 `side × side` 的格坐标索引、`+0x38` 就是陆块编号**。

> ⚠️ 那八次递归的**返回值一个都不看**，而 `0x57A430` 自己又在 `0x57A8E8`
> 恒返回 true。于是递归深处哪怕命中了上面那条"撞到别的陆块 → 整块作废"，
> 也传不回调用方。照原样复刻。

**第 2 遍的 8 个坏掩码值**（`mask(cell, 2)`）：
`0xC7 / 0x7C / 0xF1 / 0x1F / 0xC6 / 0x6C / 0xB1 / 0x1B`。
命中就把该格 `IsoTileTypeIndex` 置 `0xFFFF`、`+0x11A` 清零，再刷新八邻的 `+0x40` 缓存。
另外：`flag != 0` 时（涂块传的就是 1）还会把这一格**记到本块名下**
（`0x57A39C`：`work[cell.coord].blobId = blobId`）。刷新八邻用的是
「先写 `-1` 再现算再写回」，正好绕开上面那个缓存怪癖。

### 第 3 遍：沿岸「走线」定奇偶，再挑直边砖

四个直边分支结构完全相同，以「东边是水」（`mask & 0x02`）为例：

```c
int run = 1;
Cell* a = cell->Adjacent(4);        // 南
Cell* b = a->Adjacent(2);           // a 的东
while (!IsWater(a) && IsWater(b)) { // a 还是陆、a 的东侧还是水 → 岸线继续
    ++run;
    a = a->Adjacent(4);             // 两条线一起往南推
    b = b->Adjacent(4);
}
```

即**沿着这段直岸线一路数到头，拿 `run` 的奇偶**。四个分支的推进方向：

| 触发位 | 水在 | 起点 a | 起点 b | 推进方向 |
|--------|------|--------|--------|----------|
| `0x02` | 东 | `cell.S` | `a.E` | 南 |
| `0x20` | 西 | `cell.S` | `a.W` | 南 |
| `0x08` | 南 | `cell.E` | `a.S` | 东 |
| `0x80` | 北 | `cell.E` | `a.N` | 东 |

竖直岸线往南数、水平岸线往东数——这样相邻格数出的奇偶自然咬合，
沿岸的两种砖才会规整地交替，而不是各数各的。

选砖（`r = PickRange(0,5)`，判定顺序就是代码里的优先级 **东 → 西 → 南 → 北**）：

| 水在 | 走"素砖"的条件 | 素砖 | 否则 |
|------|----------------|------|------|
| 东 `0x02` | `(run&1 && !(mask&0x80)) \|\| !(mask&0x04) \|\| (mask&0x18)` | `0x0C` | `abs(r%3) + 9` ⚠️ |
| 西 `0x20` | `(run&1 && !(mask&0x80)) \|\| !(mask&0x10) \|\| (mask&0x0C)` | `0x1C` | `0x19 + r%3` |
| 南 `0x08` | `(run&1) \|\| !(mask&0x04) \|\| (mask&0x03)` | `0x04` | `0x01 + r%3` |
| 北 `0x80` | `(run&1) \|\| !(mask&0x01) \|\| (mask&0x06)` | `0x14` | `0x11 + r%3` |

**这就是那个随机数的全部用途**：`r % 3` 在三种同义的装饰变体里挑一个。
`r ∈ [0,5]`，`r % 3` 在 `{0,1,2}` 上均匀，`r & 1` 在 `{0,1}` 上也均匀——
同一次抽取被两类分支各取所需。为什么取 `0..5` 而不是分别取 `0..2` / `0..1`，
**未确认**（这么写只需抽一次，但没有证据说明是不是有意的）。

> ⚠️ 表里"东"那一格的 `abs` 不是笔误：只有这一支在 `0x57AEC1` 给 `r%3`
> 套了 `cdq / xor / sub`，另外三支都是直接 `edx + 常数`。`r` 恒非负，
> 所以这层 `abs` 数值上是空转。详见「复刻时踩到的四个坑」第 4 条。

四个方向都不成立时走**纯对角**分支（判定顺序 NE → SE → SW → NW）：

| 触发位 | 方位 | 砖 |
|--------|------|-----|
| `0x01` | NE | `0x23 + (r&1)` |
| `0x04` | SE | `0x21 + (r&1)` |
| `0x10` | SW | `0x27 + (r&1)` |
| `0x40` | NW | `0x25 + (r&1)` |

都不成立则原样返回成功，不贴砖。

### 第 4 遍：四种直角拐角

只看两个**相邻**正方向同时是水的四种组合，附带看那对对角：

| 触发 | 含义 | 对角对 `== 全 1` | 否则 |
|------|------|------------------|------|
| `(mask&0xA0)==0xA0` | 西+北 | `mask&0x11`：`0x17 + (r&1)` | `(mask&0x01) ? 0x15 : 0x1E` |
| `(mask&0x82)==0x82` | 北+东 | `mask&0x44`：`0x0F + (r&1)` | `(mask&0x04) ? 0x0E : 0x16` |
| `(mask&0x0A)==0x0A` | 东+南 | `mask&0x11`：`0x07 + (r&1)` | `(mask&0x01) ? 0x0D : 0x06` |
| `(mask&0x28)==0x28` | 南+西 | `mask&0x44`：`0x1F + (r&1)` | `(mask&0x04) ? 0x05 : 0x1D` |

四种都不中则直接返回成功。注意"否则"那列在原版里是用
`neg / sbb / and / add` 拼出来的无分支算式（如 `(-(mask&1 != 0) & 0xFFFFFFF7) + 0x1E`），
数值上等价于表里的三目式。

### 复刻状态：四遍编排整体已落成并对拍（✅ 2026-08-06）

`0x57A0C0` 连同它调的三个子函数（`0x57A430` / `0x57A320` / `0x57ACF0`）
已整体还原，落在 `code/rewrite/rmg/shore_smooth.{h,cpp}`。
基准由 `code/randmap/dump_shore_smooth_golden.py` 用 Unicorn **整函数模拟执行**
原版机器码取得，存档 `memory/data/rmg/shore_smooth_golden.txt`
（21 组用例、约 1.4 万行）。对拍三样：

1. 返回值（`false` = 这块地铺不出岸线，调用方整块回滚）；
2. `Randomizer::Random` 调用次数（错一次后面所有阶段的随机流全错位）；
3. 菱形内每一格的终态 `(tile, subtile, level, workBlob)`。

用例覆盖 `flag ∈ {0,1}`、圆盘/地峡/孤水点/五档密度的 LCG 噪声/全陆/全水，
以及两套合成砖尺寸剖面。实测 `0x57B12D` 处选中的下标 `i` 把
`1..0x28` **40 个值全部走到**（`i = 0` 不存在——寻址是 `[ecx+edx*4-4]`，
下标从 1 起），`(which, mask)` 组合 66 种。

> 砖尺寸剖面这一维是**为了打开分支**加的：一开始 42 块合成砖用
> `w = 1 + k%2`、`h = 1 + (k/2)%2`，多格砖会把右/下邻格提前盖住，于是
> `0x57B0ED` 那四条纯对角分支里的 `i = 0x25/0x26/0x27` 永远轮不到——
> 先被邻格那块 2×2 砖吃掉了。补一套**全 1×1** 的剖面（基准 `CASE` 行的
> `tiles=unit`）之后立刻打满。真实 `.tmp` 尺寸接上后要重取一次基准。

#### 复刻时踩到的四个坑

**1. `0xABDB64` 落点偏移表在文件映像里是全零。**
它由 CRT 静态初始化器 `0x57A9C0`–`0x57ACE8` 在运行期填出（见后文
「`[0xABDB64]` 偏移表」一节）。**任何模拟执行的对拍夹具都必须先跑一遍这段**，
否则每块砖都落在中心格上，基准整个作废。这个坑埋得很深：前 10 组用例的
返回值和取数次数**全对**，只有格子终态错位，因为偏移表只有约三分之一的
表项非零。定位办法是在 `0x57B18E` 钩出 `[esp+0x14]` 里的落点坐标逐条比对，
`i = 11` 处原版 `at=(33,24)` 而复刻 `at=(32,24)`，正好是该项的 `dx = -1`。

**2. 第 1 遍的递归返回值被丢弃。**
`0x57A430` 侵蚀成功后向八邻递归，但那八次调用的返回值一个都不看
（`0x57A8E8` 恒返回 true）。也就是说递归深处哪怕撞上了"别人的陆块"
（本该让整块作废的那条出口），也传不回来。照原样复刻。

**3. `0x57B0E8 cmp eax,-1 / jne 0x57B125` 是死代码。**
它是 `tile = -1` 那个初始化式的残留：能走到这里的只有"北边是水"那一支，
而那支刚算完 `eax = r%3 + 0x11 ∈ [0x11, 0x13]`，永远不等于 `-1`。
另外三支（`0x57AEC9` 东、西、南 `0x57B036`）也都 `jmp 0x57B0E8`，值域同样非负。
纯对角那四条分支则从 `0x57ADFB` 直接跳到 `0x57B12D`，把 `0x57B125`
的 `test eax,eax / jle` 一并跳过。归入 §九 的死代码 TODO。

**4. 只有"东边是水"那一支给 `r % 3` 包了一层绝对值。**
`0x57AEC1` 的 `cdq / xor / sub` 是标准的 `abs`，而西/南/北三支
（`0x57B035`、`0x57B0E5` 等）都是直接 `edx + 常数`。`r ∈ [0,5]` 恒非负，
`r % 3` 也就恒非负，这层 `abs` 数值上是空转——大概率是源码里那一行
多写了个 `ABS()`。照抄，不合并。

#### 一处主动偏离：`TileByIndex` 返回空

原版 `0x57B12D` 拿到 `TileTypes[ShorePieces + i - 1]` 之后**不做空指针检查**，
直接写进 `[0x880990]` 交给贴砖器。复刻里 `shore_smooth.cpp` 加了
`if (tile == nullptr) return true;`——因为 C++ 侧真取到空会直接 UB，
而原版在这种情形下是崩溃。现实里 theater 数据总是齐的，这条永远走不到，
基准也覆盖不了它；记在这里是为了不假装"逐字节一致"。

### 下标怎么变成真正的砖（✅ 已实锤）



上面各分支算出的下标 `i ∈ [1, 0x29]` 只是**组内下标**。`0x57B12D` 起：

```
0057b12d  mov edx, dword ptr [0xabad28]        ; ShorePieces
0057b133  mov ecx, dword ptr [0xa8ed2c]        ; TileTypes 数组
0057b13d  add edx, eax                         ; eax = i
0057b13f  mov edx, dword ptr [ecx + edx*4 - 4] ; 注意这个 -4
0057b143  mov dword ptr [0x880990], edx
0057b14c  mov dx, word ptr [eax*4 + 0xabdb64]  ; dx 偏移
0057b154  mov ax, word ptr [eax*4 + 0xabdb66]  ; dy 偏移
```

```c
tileObj = TileTypes[ShorePieces + i - 1];       // 那个 -4 让下标整体左移一格
placeAt = { cell.X + (int16)[0xABDB64 + i*4],
            cell.Y + (int16)[0xABDB66 + i*4] };
```

即：**`i` 与真正的砖号差 1**（`i = 1` 对应 `ShorePieces` 本身），
而且每个下标**自带一个 `(dx, dy)` 落点偏移**——多格砖要挪到左上角去贴。
`0xABDB64`/`0xABDB66` 是同一张 4 字节步长表的两个半字。

#### 选中的砖是怎么交出去的：两个全局

`0x57ACF0` 并不直接贴砖，它把选中的砖和落点**写进两个全局**，再调贴砖器：

| 全局 | 写入点 | 含义 |
|---|---|---|
| `[0x880990]` | `0x57B143` | `TileTypeClass*`，选中的那块砖 |
| `[0x88095C]` / `[0x88095E]` | `0x4A91B0` 内 | 落点 `(X, Y)`，两个 int16 |

`[0x88095C]` 就是 **`MapClass +0x1174`**（`0x87F7E8 + 0x1174 = 0x88095C`）。
`0x57B189` 调的 `0x4A91B0(&outPrev, &coord)` 在 RMG 期间只做一件事
（`0x4A92B5`，条件是 `Map->+0x117C == 0`，即当前没有建筑放置光标）：

```c
*outPrev = Map->+0x1174;      // 回吐旧值（调用方丢弃）
Map->+0x1174 = coord;         // ← 贴砖器等会儿从这里读落点
```

**它不碰地形**，只是个"当前待放置格"的 setter。

然后：

```c
[0x880990] = TileTypes[ShorePieces + i - 1];
Map->+0x1174 = placeAt;
ok = true;
if (pass == 1) TryPlace(0,          0,               cell->+0x11B, blobId, &ok, flag);
else           TryPlace(ShorePieces, ShorePieces+0x29, cell->+0x11B, blobId, &ok, flag);
if (!ok) return false;        // ← 贴不上 → 整遍中断 → 整块陆地回滚
return true;
```

`TryPlace = 0x57B440`（6 个栈参 + `ecx = MapClass`）。头两个参数是**允许覆盖的
砖号区间**：第 3 遍传 `(0, 0)`（不限制），第 4 遍传 `(ShorePieces, ShorePieces+0x29)`
（只许盖在岸砖上）。它先用 vtable `+0x2C` 校验 `[0x880990]` 的 RTTI 等于 `0x12`，
再经 vtable `+0x9C` 拿到图元，按 `+0x2E4 × +0x2E8`（砖的宽×高，多格砖）
逐格做菱形边界和上面那两张 42 项属性表的检查。

> **✅ 已复刻（2026-08-06）**：`0x57B440` 的全部控制流已还原，落在
> `code/rewrite/rmg/place_tile.{h,cpp}`，21 条用例逐条对拍原版机器码
> （`code/randmap/dump_place_tile_golden.py`，基准
> `memory/data/rmg/place_tile_golden.txt`）。它依赖的 `TileTypeClass` 三个字段
> 也都实锤了，见下面「砖块尺寸从哪来」与「贴砖器的六条出口」两节。

#### 砖块尺寸从哪来：`.tmp` 文件头（`0x546B4A`）

`TileTypeClass +0x2E4` / `+0x2E8` 不是 ini 里的值，而是**直接抄自 `.tmp`
文件头的两个字节**：

```asm
00546B46  mov al,  byte ptr [ebx]         ; .tmp +0x00
00546B4A  mov [ebp+0x2E4], eax            ; 占地宽（格）
00546B50  mov cl,  byte ptr [ebx+4]       ; .tmp +0x04
00546B53  mov [ebp+0x2E8], ecx            ; 占地高（格）
```

注意是 `mov al` / `mov cl` —— **按字节读**，高 24 位来自前面的 `xor eax,eax`。
紧接着 `0x546B59` 用 `[ebx] * [ebx+4]` 当子块个数遍历偏移表，把表里的相对
偏移就地改成绝对指针（`0x546B66`-`0x546B80`）。所以 `.tmp` 的布局是：

| 偏移 | 含义 |
|---|---|
| `+0x00` | 占地宽（格），dword 存但只读低字节 |
| `+0x04` | 占地高（格），同上 |
| `+0x08` / `+0x0C` | 单块像素宽 / 高（实测恒为 60 / 30） |
| `+0x10` 起 | `宽 × 高` 个 dword 偏移；**为 0 表示这个子格是空洞** |

子块自己的头里，贴砖器只用 `+0x28` 那个字节 —— 该子格的**高度增量**。

实测（`ra2.mix` → `isotemp.mix`，用 `code/mix/mix_read.py` 读）：
`water01.tem` 是 `2×2`、`water09.tem` 是 `1×1`、`shore04.tem` 是 `1×2`、
`shore42.tem` 是 `9×5`；`cliff01.tem` 的 `2×3` 里有两个空洞，
剩下四格的 `+0x28` 是 `[4, 0, 4, 0]`。空洞与高度增量两条都在基准里验过。


#### 真实 `.tmp` 几何已全量导出并进了基准（✅ 2026-08-06）

`code/randmap/dump_tile_geometry.py` 把 RMG 阶段 2 真正会用到的两组砖
（`ShorePieces` 42 块 + `WaterSet` 14 块，温和 + 雪地共 112 条）的几何
全部读了出来，存档 `memory/data/rmg/tile_geometry.txt`：每块砖一行
`TILE <气候> <组> <砖号> <文件> <宽> <高>`，每个子格一行
`SUB <k> <present> <height> <ramp>`。**只有几何数值，没有像素/调色板**。

装载器那条相对偏移→绝对指针的就地重定位（`0x546B66`-`0x546B80`）是关键：
偏移表里为 0 的项被跳过（保持 0），其余加上映像基址。所以「0 = 空洞」这条
不是猜的，是装载器自己定义的。

读出来的真值：

| 组 | 尺寸分布 | 高度增量 | RampType | 空洞 |
|---|---|---|---|---|
| ShorePieces（42） | 2×2 ×28、2×3 ×4、3×2 ×4、1×2 ×2、2×1 ×2、6×4 ×1、9×5 ×1 | **全 0** | **全 0** | 只有 `shore41`（3 个）/ `shore42`（15 个） |
| WaterSet（14） | **前 8 块 2×2、后 6 块 1×1** | 全 0 | 全 0 | 无 |

三条由此得到的交叉验证 / 新结论：

1. **水砖的 8 + 6 分布，与 `0x59C630` 的 `shape = n / 40` 严丝合缝。**
   大水面那支压 `shape ∈ [0,7]`（正好 8 块 2×2），小浪花那支取
   `WaterSet + 8 + n/40 ∈ [8,13]`（正好 6 块 1×1）。这是继「魔数
   `0x66666667` 配 `sar edx,4`」之后，`/40` 的**第二条独立证据**——
   若真是 `/10`，`shape` 能到 23，早就越过 `TilesInSet = 0x0E` 了。
2. **`shore41` / `shore42` 岸线平滑永远选不到。** 第 3/4 遍算出的组内下标
   `i ∈ [1, 0x28]`，砖号 = `ShorePieces + i - 1`，最大只到 `shore40`。
   那两块带空洞的大砖（6×4 与 9×5）只可能作为**既有地形**出现在
   allow 区间（`ShorePieces + 0x29`）里，不会被这个阶段贴上去。
3. **温和与雪地这 56 块砖的几何完全相同**，只有像素不同。
   所以对拍夹具一套 `real` 剖面对两个气候都成立。

于是三份基准（岸线 / 阶段 2 / 水面细化）各加了一档 `tiles=real` 的用例，
C++ 侧由 `code/rewrite/rmg/test/tile_geometry_fixture.h` 读同一份表——
两边共用唯一真值来源。**合成剖面没有删掉**：真实数据的高度增量与
RampType 全是 0、可达的那 40 块岸砖又都没有空洞，压不出「层高累加」和
「空洞跳过」两条分支；反过来真实数据能压出合成规则给不出的
9×5 大砖（一块盖 45 格）与各种非对称尺寸。三套剖面互补，都留着。
#### 贴砖器的六条出口（`0x57B440`）

外层 `ty` 走 `+0x2E8`、内层 `tx` 走 `+0x2E4`，逐子格判定。落点来自
`[0x88095C]`（两个 `int16`），**两个分量各自先截成 int16 再符号扩展**
（`0x57B4C4`-`0x57B4E1` 全程在 `cx`/`ax` 上算）。子格号 `k = 砖宽 × ty + tx`，
写进 `CellClass +0x11A`；`+0x11B` 写 `子块高度增量 + base_level`。

判定按工作数组 `+0x38`（这一格归谁）分三支，出口一共六条：

| # | 条件 | 返回 | `*ok` | 地址 |
|---|---|---|---|---|
| 1 | 归属相同，且两块砖的**朝向差** `∈ [3, 5]` | false | 清 | `0x57B724` |
| 2 | 归属是别的块、旧砖已定、**不同族** | false | 清 | `0x57B711` |
| 3 | 归属是别的块、旧砖已定、**同族** | **true** | 不动 | `0x57B705` |
| 4 | 其余情形、旧砖已定、`blob_id != -1` | false | 清 | `0x57B77D` |
| 5 | 旧砖已定、不在 allow 区间、且受保护 | false | 清 | `0x57B774` |
| 6 | 旧砖已定、不在 allow 区间、**不受保护** | false | **不动** | `0x57B784` |

> ⚠️ **第 3 条像漏了个 `continue`**：只要撞上一格"别人的、同族的"砖，
> 整块砖就**当场判成功返回**，后面的子格一格都不贴。基准实测确认
> （用例 `otherblob-same-family`：2×2 的砖只有第一格被写下去）。
>
> ⚠️ **第 6 条是静默失败**：返回 `false` 却不碰 `*ok`。而调用方 `0x57B1E8`
> **只看 `*ok`、不看返回值** —— 所以这条路对调用方等于"没失败"，
> 那块砖就这么被悄悄丢了。两条都照原样复刻。
>
> ⚠️ 还有一条反直觉的：allow 区间**包住**旧砖号时也可能失败。因为归属
> 判定在 allow 那道闸**之前**，归属对不上就先从第 4 条出口走了，
> allow 根本没轮到（用例 `inside-allow`）。

`[esp+0x44]` 那个标志不是"只试探不写入"——它做的是 `0x57B463` 把
`blob_id` 强行清零，于是"归属相同"恒成立、工作数组既不读也不写，
但砖**照写不误**。

> ⚠️ **更正（2026-08-06）**：早前这里写的是"岸线第 3/4 遍都传 0"，错了。
> `0x57B1A4 push ecx` 与 `0x57B1BE mov edx,[esp+0x30]` 推的都是 `0x57ACF0`
> 自己栈上的 `flag`，也就是 `Shoreline` 的第二个参数原样往下传。
>
> `Shoreline` 全镜像共 5 个调用点，`flag` 各不相同：
>
> | 调用点 | 所在 | `blobId` | `flag` |
> |---|---|---|---|
> | `0x59C433` | 涂块 `0x59BBC0` 收尾 | `+0x308` 当前块号 | **1** |
> | `0x59A788` | 阶段 2 调度 `0x59A6C0` 收尾 | 0 | **0** |
> | `0x59D282` | 斑点实现 B（`0x59C920`） | `+0x308` | 0 |
> | `0x59E260` | — | `+0x308` | `edi`（变量） |
> | `0x59EC86` | — | `+0x308` | 0 |
>
> 所以**不能假定 `flag` 恒为 1**：涂块每铺一块地是 1，阶段收尾那次全图平滑是 0。
> `flag = 1` 时工作数组的归属判定在第 3/4 遍被整个旁路掉，`flag = 0` 时才真正生效。

#### 两份判据不一致：`0x4863D0` 与 `0x578D80`

两个都在问"这块地形能不能被覆盖"，判据几乎一样却不完全一样，
而且**都被同一次贴砖用到**（`0x57B739`/`0x57B74E` 与 `0x57B772`）：

| | `0x4863D0`（读格子） | `0x578D80`（读砖号+子格号） |
|---|---|---|
| 每组先查 `-1` | ✅ 查 | ❌ 不查 |
| CliffSet (0x28) | ✅ | ✅ |
| 四组瀑布 (各 4) | ✅ | ✅ |
| CliffRamps (0x14) | ✅ | ✅ |
| WaterCaves (4) | ✅ | ❌ |
| BridgeSet (0x10) | ✅ | ❌ |
| WoodBridgeSet (0x10) | ✅ | ❌ |
| DestroyableCliffs (2) | ✅ | ❌ |
| WaterCliffs (0x1C) | ✅ | ❌ |

四组瀑布的形状一致：组宽 4，组内第 1/2 块**无条件保护**，第 0/3 块要看
子格号，各有两个"反而放行"的例外——东 `{0, 4}`、西 `{1, 3}`、
南 `{0, 1}`、北 `{2, 3}`。不查 `-1` 的那份在该 theater 没定义某组时会拿
`-1` 去比区间，`tile >= -1 && tile < 3` 对小砖号可能误命中；temperate 和
snow 两个气候下这几组都有定义，所以现网跑不出来。照原样各写一份，不合并。


#### `[0xABDB64]` 偏移表：CRT 静态初始化，不随 theater 变

它在 `.bss` 里，但**填表的是一个 C++ 静态初始化器 `0x57A9C0`**——
函数指针躺在 `.data` 的 `0x813AB4`，由 CRT 的 `_initterm` 在 `WinMain` 之前跑一次。
全 exe 没有任何 `call 0x57A9C0`，所以从前只看调用图会以为这张表是空的。

用 `code/randmap/emu_oracle.py` 的 `run_range(0x57A9C0, 0x57ACE8)` 模拟执行后 dump，
43 项（下标 `0..0x2A`）全表见 `memory/data/rmg/shore_tables.txt`。

> ⚠️ **在文件映像里这张表是全零的**。任何用 Unicorn/QEMU 跑岸线平滑的对拍
> 夹具都必须**先执行一遍 `0x57A9C0`–`0x57ACE8`**，否则每块砖都会落在中心格上。
> `dump_shore_smooth_golden.py` 的 `SmoothOracle.__init__` 里就是干这个的。

取值只有 `{0, -1, -2}`，摘要：

| `i` | `(dx, dy)` |
|---|---|
| `0x01`–`0x04` | `(0, -1)` |
| `0x05` | `(0, -2)` |
| `0x06` | `(-1, -2)` |
| `0x07`–`0x08` | `(-1, -1)` |
| `0x09`–`0x0C` | `(-1, 0)` |
| `0x0D` | `(-2, -1)` |
| `0x0E` | `(-2, 0)` |
| `0x0F`–`0x10` | `(-1, 0)` |
| `0x16` | `(-1, 0)` |
| `0x1D` `0x1F` `0x20` | `(0, -1)` |
| `0x21`–`0x22` | `(-1, -1)` |
| `0x23`–`0x24` | `(-1, 0)` |
| `0x27`–`0x28` | `(0, -1)` |
| 其余 | `(0, 0)` |

#### 两张 42 项属性表：也是静态数据，与 theater 无关

`0x82A7F4` / `0x82A89C` 都在 `.data`，是写死在 exe 里的 `int[42]`，
索引 `k = tileIndex - ShorePieces ∈ [0, 0x29]`（= `i - 1`），越界即判失败：

```
0057b5af  cmp eax, 0x29 / jg  失败          ; 两个索引都要在 [0, 0x29]
0057b5b9  mov eax, dword ptr [eax*4 + 0x82a89c]
0057b5c0  mov edx, dword ptr [ecx*4 + 0x82a89c]
0057b5c7  sub eax, edx / cdq / xor / sub    ; |A - B|
0057b5ce  cmp eax, 3 / jl  失败
0057b5d3  cmp eax, 5 / jle 通过             ; 即 |A - B| ∈ [3, 5]
```

```
0057b6f3  mov eax, dword ptr [edi*4 + 0x82a7f4]
0057b6fa  mov ecx, dword ptr [ebp*4 + 0x82a7f4]
0057b701  cmp eax, ecx / jne 失败           ; 必须完全相等
```

| 表 | 值域 | 判据 | 语义（**推测**） |
|---|---|---|---|
| `0x82A7F4` | `0..25` | 两块砖的值必须**相等** | 砖的"形状族"编号 |
| `0x82A89C` | `0..7` | 两块砖的值之差绝对值必须 ∈ `[3, 5]` | 8 方向朝向；差 3–5 ≈ 135°–225°，也就是"朝向大致相反" |

全表见 `memory/data/rmg/shore_tables.txt`。数值是实锤的，**语义命名是推测**。

`eax` / `ecx` 的来源（`0x57B58C-0x57B5A1`）：
前者是**已在图上那块砖**的 `cell->+0x38`，后者是**候选砖**的 `TileTypeClass->+0x294`，
都减掉 `ShorePieces`。

## 八、随机数消耗账本（复刻对齐的核心）

种子兼容性的全部内容就是"阶段顺序 + 每阶段取数个数"。本阶段已能逐条列出：

| 位置 | 每次消耗 `Random()` | 备注 |
|------|--------------------|------|
| MapType 0：`r = PickAtMost(n)` | ≥1（拒绝采样） | 每次拒绝再吃 1 个 |
| `MakeGrid` 抛硬币 | 1 | |
| `MakeGrid` 分摊多余格 | ≥1 × `rem` | `rem = rows*cols - n` |
| MapType 0 每块的预算 | 1 | |
| MapType 2 抛硬币切半 | 1 | |
| 涂块：随机起点（仅起点为 `{0,0}` 时） | ≥2 × 尝试次数 | 上限 200 次 |
| 涂块：每个候选格的抖动（优先级模式 0） | 1 | |
| 涂块：每步中心布朗运动 | 2 个正态偏差 | 见下 |
| **岸线平滑第 3 遍** | **每格 1**（`PickRange(0,5)`） | 见 §七，**消耗大头** |
| **岸线平滑第 4 遍** | **每格 1** | 同上 |
| 水面细化 `FUN_0059C630` | 每格 1~2 | 已还原并对拍 |

**正态偏差的消耗最不直观**：`NormalDeviate` 一次算两个、缓存第二个，
所以「两次调用」才消耗一轮；而一轮里 Marsaglia 拒绝（落在单位圆外）
会**额外**吃 2 个 `Random()`。每步中心游走正好调两次 → 每步稳定消耗一轮。
详见 [fast-math.md](fast-math.md) §三。

岸线平滑的量级最容易被低估：它是**每块陆地收尾时对全图扫两遍、每格各抽一次**，
因此本阶段的随机数消耗由它主导，`2 × 扫到的格子数 × 涂块次数` 起步。

这条差点被漏掉：先只搜了 `call Randomizer::Random`，四个平滑函数体内确实一处都没有，
险些写下"平滑不消耗随机数"。实际是隔了一层包装（`0x57ACF0 → 0x598030 → Random`）。
补救办法是做**调用树的传递闭包**，并且用 Ghidra 的真实函数边界而非线性反汇编猜边界
（线性猜边界会跑飞：同一次扫描"可达"2525 个函数，用真边界只有 93 个）。
脚本 `code/ghidra_scripts/rng_closure.py`，结果存档 `memory/data/decomp/rng_closure.txt`：

```
根 0x57A0C0（岸线）      可达 93 个函数，取数点 1 处
  Random <- 00598030 <- 0057ACF0 <- 0057A0C0
根 0x59BBC0（涂块）      可达 101 个函数，取数点 2 处
根 0x59A6C0（阶段调度）  可达 110 个函数，取数点 5 处
```

**方法论**：判断"某段代码会不会动随机数"，必须做传递闭包 + 精确函数边界，
只搜直接调用会漏掉包装层。

## 九、下一轮

1. ~~`FUN_0057B210` 的八邻域掩码定义~~ ✅ 已实锤，见 §七
2. ~~第 3 遍「走线奇偶」在数什么~~ ✅ 已实锤，见 §七
3. ~~四遍平滑各分支产出的 tile 序号表~~ ✅ 已实锤，见 §七末：
   `tile = TileTypes[ShorePieces + i - 1]`、落点偏移表 `[0xABDB64]`（CRT 静态初始化器
   `0x57A9C0` 填的，已模拟执行 dump）、两张 `.data` 静态属性表 `0x82A7F4` / `0x82A89C`
   的判据也已实锤（语义命名仍是推测）。全表存档 `memory/data/rmg/shore_tables.txt`
4. ~~`FUN_0042B1F0`「可用格子总数」的口径~~ ✅ `= (H + 4) * W * 2`，见
   [stage-init.md](stage-init.md) §六
5. 确认工作记录 0x50 字节的完整字段布局
   （已锁定：数组按 `side × side` 的格坐标索引；`+0x38` 陆块编号 / `+0x3C` 入堆标记 /
   `+0x40` 掩码缓存（`-1` = 失效）/ `+0x4A` 有效位 / `+0x4B`）
6. ~~确认 `0xAA0738` 等 tile 组基址由 theater 决定的具体来源~~
   ✅ 已打通 → [theater-data.md](theater-data.md)（TileSet 枚举的前缀和）
7. ~~需要先有地图/格子容器~~ ✅ 已就位 → [map-geometry.md](map-geometry.md)
   （菱形四不等式 + 扫全图顺序 + `N = H(2W−1)`，`code/rewrite/rmg/map_grid.h`）
8. ~~涂块最小堆的并列语义~~ ✅ 已实锤并对拍，见 §四「最小堆本身」
9. ~~`InRegion` / `DirectionalCost` 落成 C++~~ ✅ 已实锤并对拍，见 §四
10. ~~把**涂块主循环**落成 C++~~ ✅ 整函数已落成并对拍，见 §四「复刻状态」
    （`blob_paint.{h,cpp}` + 14 组用例约一万格逐格一致；随机起点段与收尾/回滚都在内）
11. ~~岸线平滑 `FUN_0057A0C0`~~ ✅ **整体已落成并对拍**，见 §七「复刻状态」。
    掩码 `0x57B210`、贴砖器 `0x57B440`、四遍编排全部到位，21 组用例、
    40 个砖下标全覆盖。第 3/4 遍每格各抽一个 `PickRange(0,5)`，
    是本阶段随机数消耗的大头（见 §八）
12. ~~`MakeGrid 0x59A8F0`~~ ✅ 已落成并对拍，见 §三末尾
13. ~~三种 MapType 投放策略（`0x59AD10` / `0x59AFA0` / `0x59B200`）~~
    ✅ 已落成并对拍，见 §二末尾。至此**阶段 2 里不依赖 theater 数据的部分全部收口**：
    投放策略 → 区域网格 → 涂块核心 → 八邻域掩码，一条链跑通
14. ~~theater 数据链~~ ✅ 已打通，见 [theater-data.md](theater-data.md)
15. ~~贴砖器 `0x57B440`~~ ✅ 已落成并对拍，见 §七「贴砖器的六条出口」。
    `TileTypeClass +0x2E4/+0x2E8` 实锤为 `.tmp` 文件头的两个字节（`0x546B4A`）
16. ~~把四遍平滑的**编排**串起来~~ ✅ 已完成，见第 11 条
17. ~~把 `TheaterData` 接进 `PaintWorld` / `ShoreWorld`~~ ✅ 已完成：
    删掉了 `WaterTile()` / `ShorePiecesBase()` 两个虚函数，改从 `TheaterData`
    取——这既去掉了重复真值来源，也让阶段 2 那个"既是涂块世界又是平滑世界"
    的多继承类不再因 `WaterTile` 二义而无法编译
18. ~~阶段 2 调度器 `FUN_0059A6C0`~~ ✅ 已落成并对拍，见 §一末尾。
    **至此阶段 2 的 `MapType ∈ {0,1,2}` 主路径整条闭合**：
    铺水 → 投放策略 → 涂块 → 孤水格回填 → 岸线四遍 → 工作数组清账 → 沿岸刷绿
19. ✅ **已完成（2026-08-06）**：真实 `.tmp` 几何已导出为
    `memory/data/rmg/tile_geometry.txt`，岸线 / 阶段 2 / 水面细化三份基准
    各加一档 `tiles=real` 用例，18/18 对拍全绿（见上「真实 `.tmp` 几何」一节）。
    仍欠的是**运行期**从 mix 直接读 `.tmp`——目前只有 Python 侧的
    `code/mix/mix_read.py`，C++ CLI 那条路等出图阶段再补
20. ~~水面 tile 细化 `0x59C630` + 压砖器 `0x5A6C10`~~ ✅ 已落成并对拍，见 §六。
    顺手纠了旧文一处实质错误：大水面的形状号是 `n / 40` 不是 `n / 10`（魔数
    `0x66666667` 配 `sar edx,4`），于是 `shape ∈ [0,7]` 正好与小浪花的 `[8,13]`
    拼满 WaterSet 的 `TilesInSet = 0x0E`；新记 `0x5A6C10` 会写 `CellClass +0x11C`
    （坡度）而 `0x57B440` 不写，且它的层高公式多一个 `-4`
21. ~~`MapType ∈ {3,4}` 那条支路：`0x59C580`~~ ✅ **算法已全还原**，
    独立成篇 → [stage-water-maptype34.md](stage-water-maptype34.md)。
    门槛纠正：是 `WaterAmount > 20` 而不是「非零」。含湖 `0x59C920`、
    河 `0x59D510`、三个区域算子 `0x5A0700`/`0x5A0410`/`0x5A0160`，
    以及新查实的查表 sin/cos `0x4CACB0`/`0x4CAD00`（**已落 C++ 并对拍**）。
    另含瀑布 `0x59E740`（**不是桥**，见该文 §六与 §八的更正记录）与角度扩张
    `0x5A08D0`（迭代次数公式 `n = _ftol(0.5·max(ln k, 1)/weight)` 已破）。
    顺手关掉 `MapSeedClass +0x310` 的用途：它决定这张图的河上有没有**瀑布**。
    `FUN_004CADE0` 也已查实是**查表 atan**（4097 项 @`0x8610B4`，连表的生成式
    都还原了），已落 C++ 并逐位对拍——**本支路要用的数学基元至此全部完备**。
    仍欠：整支路的 C++ 落地
22. 阶段 4「Making regions」（45 次取数，只在 `MapType ∈ {3,4}` 下走）——
    **骨架与末段三个函数已实锤**，见 [stage-making-regions.md](stage-making-regions.md)：
    `0x5A1350` 的 21 路 switch 已破（CliffSet 的 6 个同形三连组 + 一对 `28↔29`，
    永不返回原砖），`Accessibility` 的生效点找到了（`0x5905D0`）。
    仍欠 22 个取数点，集中在 `0x590970`（开坡道）/ `0x58F2C0`（跨水连接）/
    `0x58D620`（区域切分）三棵子树
23. **死代码 TODO**：`0x57B0E8` 的 `cmp eax,-1`（§七「四个坑」第 3 条）、
    `0x545FE5` 的 `LastTilesInSet` 死分支，以及那 22 个从未被调用的函数
    （46 个取数点，27 个在 `0x5A5020` 子树里）。按约定推迟到 104 个活取数点
    做完、逐字节对比闭环之后再逆
