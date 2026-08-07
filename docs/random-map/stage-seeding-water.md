# 阶段：Seeding water（水域播种）

> 状态：**涂块核心 `0x59BBC0` 已于 2026-08-06 整函数落成 C++ 并对拍通过**
> （`code/rewrite/rmg/blob_paint.{h,cpp}` / `test_blob_paint.cpp`，14 组用例、
> 约一万格**逐格生长顺序**与原版完全一致，随机数取数次数也逐组吻合，见 §四末尾）；
> 岸线平滑的**八邻域掩码 `0x57B210`**（§七，1299 次查询）与
> **区域网格 `0x59A8F0`**（§三，16 组用例）也已落成并对拍。
> 三种 MapType 的投放策略、四遍平滑的判定逻辑已还原但**尚未落成 C++**；
> 贴砖器 `0x57B440` 卡在 theater 的 `TileTypeClass` 数据上（§七末）。
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
int r = PickAtMost(n);                        // 浮点比例+拒绝采样，r ∈ [0, n]
MakeGrid(&list, NumPlayers + r, &FullBounds); // 0x59A8F0，见 §三
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
    painted += Paint(budget, &region, &center, 1/*椭圆*/, &center, 0.75, 1/*方向性*/);
    // 下一个中心 = 全图中「+0x3C == 0 且到当前中心曼哈顿距离最小」的格子
    center = argmin_{cell: work[cell].queued == 0} (|cell.Y - center.Y| + |cell.X - center.X|);
    if (painted / total >= target) break;
}
```

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
    ...与 MapType 1 相同的生长循环，但区域是这一半、目标是 target...
    // 下一中心的代价换成**各向异性加权**的曼哈顿距离：
    //   cost = |cell.Y - cy| * aspectY + |cell.X - cx| * aspectX
    // 且候选必须先过 InRegion（内切椭圆，0x59BAB0）
}
```

两点差异值得注意：

1. **目标覆盖率降到 0.15~0.20**——因为是"每半各占这么多"，且两半的
   `painted/total` 分母仍是**全图**可用格数，合起来仍是 0.3~0.4 量级；
2. 选下一中心时扫的是**工作数组**（`side²` 项）而不是地图格子迭代器，
   先用等距坐标做包围盒粗筛，再过椭圆判定，最后取加权曼哈顿最小者。

各向异性系数与涂块核心里的一致（`0x7E5190 = 1.2`）：
长边取 `1.0`，短边取 `(长/短) × 1.2`。

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

## 六、水面 tile 细化 `FUN_0059C630`（已完整还原）

对每个"是水且 `+0x11A == 0`"的格子：

```c
if (右/下/右下三邻格也都是水且 +0x11A == 0) {
    a = PickFrom1(10);                       // 1..10，常数 0x7ED9E8
    if (a != 1) {
        b = PickBelow(242);                  // 0..241，常数 0x7ED9E0
        shape = (b >= 240) ? (247 - b) : (b / 10);     // 0..23 或 6..7
        FUN_005A6C10(work[cell].field_38, -1);          // 用 shape 贴大块水面
        continue;
    }
}
c = PickBelow(201);                          // 0..200，常数 0x7ED9D8
cell->IsoTileTypeIndex = [0xAA0738] + 8 + c / 40;      // 8..13 号小水面
```

三处取数的比例常数、拒绝阈值、整数除法魔数（`0x66666667`→/10、
`0xCCCCCCCD`→/40）都已逐条汇编核对，
基准值见 `memory/data/rmg/scaled_golden.txt`，重写已对拍通过。

## 七、岸线平滑 `FUN_0057A0C0`——并且**它会消耗随机数**

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
| 东 `0x02` | `(run&1 && !(mask&0x80)) \|\| !(mask&0x04) \|\| (mask&0x18)` | `0x0C` | `0x09 + r%3` |
| 西 `0x20` | `(run&1 && !(mask&0x80)) \|\| !(mask&0x10) \|\| (mask&0x0C)` | `0x1C` | `0x19 + r%3` |
| 南 `0x08` | `(run&1) \|\| !(mask&0x04) \|\| (mask&0x03)` | `0x04` | `0x01 + r%3` |
| 北 `0x80` | `(run&1) \|\| !(mask&0x01) \|\| (mask&0x06)` | `0x14` | `0x11 + r%3` |

**这就是那个随机数的全部用途**：`r % 3` 在三种同义的装饰变体里挑一个。
`r ∈ [0,5]`，`r % 3` 在 `{0,1,2}` 上均匀，`r & 1` 在 `{0,1}` 上也均匀——
同一次抽取被两类分支各取所需。为什么取 `0..5` 而不是分别取 `0..2` / `0..1`，
**未确认**（这么写只需抽一次，但没有证据说明是不是有意的）。

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

> **未确认 / 未复刻**：`0x57B440` 依赖真实的 `TileTypeClass` 对象
> （`+0x294` 砖号、`+0x2E4/+0x2E8` 尺寸、vtable `+0x2C`/`+0x9C`），
> 这些是 theater 的 `.ini` + `.tmp` 数据，本项目还没接。
> 因此**岸线平滑目前只复刻到掩码这一层**，四遍的贴砖与失败判定要等
> theater 数据接进来之后才能落地。这是当前离"端到端逐字节"最近的一块硬骨头。

#### `[0xABDB64]` 偏移表：CRT 静态初始化，不随 theater 变

它在 `.bss` 里，但**填表的是一个 C++ 静态初始化器 `0x57A9C0`**——
函数指针躺在 `.data` 的 `0x813AB4`，由 CRT 的 `_initterm` 在 `WinMain` 之前跑一次。
全 exe 没有任何 `call 0x57A9C0`，所以从前只看调用图会以为这张表是空的。

用 `code/randmap/emu_oracle.py` 的 `run_range(0x57A9C0, 0x57ACE8)` 模拟执行后 dump，
43 项（下标 `0..0x2A`）全表见 `memory/data/rmg/shore_tables.txt`。
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
6. 确认 `0xAA0738` 等 tile 组基址由 theater 决定的具体来源
7. ~~需要先有地图/格子容器~~ ✅ 已就位 → [map-geometry.md](map-geometry.md)
   （菱形四不等式 + 扫全图顺序 + `N = H(2W−1)`，`code/rewrite/rmg/map_grid.h`）
8. ~~涂块最小堆的并列语义~~ ✅ 已实锤并对拍，见 §四「最小堆本身」
9. ~~`InRegion` / `DirectionalCost` 落成 C++~~ ✅ 已实锤并对拍，见 §四
10. ~~把**涂块主循环**落成 C++~~ ✅ 整函数已落成并对拍，见 §四「复刻状态」
    （`blob_paint.{h,cpp}` + 14 组用例约一万格逐格一致；随机起点段与收尾/回滚都在内）
11. 岸线平滑 `FUN_0057A0C0`：**掩码 `0x57B210` 已落成 C++ 并对拍**（见 §七）；
    四遍的判定逻辑也已实锤，但**贴砖器 `0x57B440` 依赖 theater 的 `TileTypeClass` 数据**，
    要等第 6 条（tile 组基址来源）解决后才能落地。
    第 3/4 遍每格各抽一个 `PickRange(0,5)`，是本阶段随机数消耗的大头（见 §八），
    不做完，账本对不上，涂块之后的一切都会错位
12. ~~`MakeGrid 0x59A8F0`~~ ✅ 已落成并对拍，见 §三末尾
13. 下一件：三种 MapType 投放策略（`0x59AD10` / `0x59AFA0` / `0x59B200`）
    落成 C++，把 `MakeGrid` + `PaintBlob` 串成完整阶段——这三个函数不依赖
    theater 数据，是眼下唯一还能纯算法推进的一段
