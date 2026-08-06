# 阶段：Seeding water（水域播种）

> 状态：**涂块核心 + 三种 MapType 的投放策略均已完整还原**；
> 缺口只剩岸线平滑 `FUN_0057A0C0`（它决定一块陆地是被接受还是被抹回海里）。
> 本文只记录已取证的部分，推测一律标注「未确认」。
> 涉及的数学基元见 [fast-math.md](fast-math.md)（已逐位对拍）。

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
double total  = (double)CountUsableCells();                        // 0x42B1F0
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

## 四、涂块核心 `FUN_0059BBC0`——陆地形状的全部来源

**已完整还原**。本质是一次 **Dijkstra 式的最小堆扩散**：从一个种子格出发，
每次取出"优先级最小"的候选格把它变成陆地，再把它的四个邻格塞回堆里。
优先级 = `FastSqrt(到引力中心的距离)` + 随机抖动，而**引力中心自己在做布朗运动**
——不规则的海岸线就是这么来的。

### 参数

| 位置 | 含义 | 已观察到的取值 |
|------|------|----------------|
| `ecx` | `MapSeedClass` | `0xABDFD8` |
| `[ebp+0x08]` | 预算（要涂多少格） | 小于 401 一律提到 400 |
| `[ebp+0x0C]` | 区域 `int[4] {bx, by, bw, bh}`（真实坐标） | 由投放策略给出 |
| `[ebp+0x10]` | 起点格；两分量都为 0 时随机取 | |
| `[ebp+0x14]` | 区域形状：0=矩形，非 0=内切椭圆 | |
| `[ebp+0x18]` | 方向性代价的目标点 | 仅优先级模式 ≠0 时用 |
| `[ebp+0x1C]` | double 中心游走步长 | MapType 0 用 `0.25`，MapType 1 用 `0.75` |
| `[ebp+0x24]` | 优先级模式：0=距离+抖动，非 0=方向性代价 | |

返回涂成功的格数；整块被否决时返回 0。

### 主循环

```c
if (budget < 401) budget = 400;
poolCap = max(budget * 8 + 2, 100);
nodes = new Node[poolCap];        // Node { int16 x, y; float priority; }  共 8 字节
heap  = new MinHeap(poolCap);     // 1 基二叉最小堆，比较 *(float*)(node+4)

// 长宽比各向异性系数（常数 0x7E5190 = 1.2）
(ax, ay) = (bw > bh) ? (1.0, (bw/bh)*1.2) : ((bh/bw)*1.2, 1.0);
ellipseA = 1/(bw*0.5)^2 ;  ellipseB = 1/(bh*0.5)^2;      // 0x7E1738 = 0.5

double cx = start.x, cy = start.y;        // 引力中心（浮点，会漂移）
push(start, 0.0f);
painted = 0;

while (painted < budget && heap 非空) {
    n = pop();                                    // 优先级最小者
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
            pr = DirectionalCost(p, n, region, 目标点, ax, ay);          // 0x59B940
        }
        push(p, (float)pr);                       // 注意：存成 float，不是 double
        work[p].queued = 块编号;
    }

    cx += Normal() * 步长;                        // ← 引力中心做布朗运动
    cy += Normal() * 步长;
    painted++;
}
```

抖动项 `Random() * 5.0 * (1/4294967295.0)` 即均匀 `[0, 5)`——
相当于给"到中心的距离"加上最多 5 格的噪声，边界因此变毛糙而不是圆滑。
乘法顺序（先 ×5 再 ×倒数）已按汇编照抄，换序会在 1 ulp 上分岔。

### 收尾与整块回滚

```c
while (heap 非空) {                  // 预算用尽，把队列里剩下的边缘格也铺成陆地
    n = pop();
    if (work[n].owner == 0 && IsPaintable(MapAt(n))) {
        MapAt(n)->IsoTileTypeIndex = 0;
        work[n].owner = 块编号;
    }
}
if (!FUN_0057A0C0(块编号, 1)) {       // 铺岸线过渡砖；铺不出来就否决整块
    for (每个 owner == 块编号 的格子) {
        work[..].owner = 0;  work[..].+0x4B = 0;
        cell->IsoTileTypeIndex = [0xAA0738];      // 还原成水
        cell[+0x11A] = 0;  cell[+0x11B] = MapSeedClass[0x30C];
    }
    return 0;                        // ← 调用方据此重试（MapType 0 最多试 10 次）
}
MapSeedClass[0x308]++;
return painted;
```

`FUN_0057A0C0` 不只是校验：它遍历全图跑四遍平滑（`0x57A430` / `0x57A320` /
`0x57ACF0(...,1)` / `0x57ACF0(...,2)`），顺带在工作数组缺席时把它建出来。
**任一遍返回失败，整块陆地就被抹回海里**——这是"地图有时大片是海"的直接机制。

### 邻接方向表 `0x89F688`（初始化于 `0x49F2F0`）

`.bss` 里的 8 项 `{int16 dx, int16 dy}`，运行期由 `0x49F2F0` 写入：

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| (dx,dy) | (0,-1) | (1,-1) | (1,0) | (1,1) | (0,1) | (-1,1) | (-1,0) | (-1,-1) |

涂块用步长 2 取 `0/2/4/6` → `(0,-1) (1,0) (0,1) (-1,0)`。
这是**等距坐标**下的四个基本方向，对应真实坐标里的四个对角邻格。

### 区域判定 `FUN_0059BAB0`

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
注意矩形分支拿的是等距坐标、椭圆分支拿的是真实坐标——
两条分支口径不一致，**是否为原版口误未确认**，端到端对拍时再定。

### 方向性代价 `FUN_0059B940`（优先级模式 ≠ 0）

```c
if (region 就是全图边界) return FastSqrt(dy² + dx²);     // 退化成欧氏距离
ax_ = |Δrx(n → 目标点)| ;  ay_ = |Δry(n → 目标点)|;      // 由 (x∓y)>>1 得到
if (ax_ == 0 && ay_ == 0) return 0.0;
len  = FastSqrt(ay_² + ax_²);
cheb = max(|p.y - n.y|, |p.x - n.x|);                    // 切比雪夫步距
return cheb * (aspectX * (ax_/len) + (ay_/len) * aspectY);
```

即：**步距按"朝目标点的方向"做各向异性加权**，让这块陆地朝指定方向拉长。

### 可涂性谓词 `FUN_0004865D0`

只看 `cell->IsoTileTypeIndex`（`+0x38`），落进下列任一区间即可涂：

| 区间 | 长度 |
|------|------|
| `[ [0xABAD28], +0x2A )` | 42 |
| `[ [0xAA0738], +0x0E )` | 14（水面族） |
| `[ [0xAA073C], +4 )` / `[ [0xABB110], +4 )` / `[ [0xAA1050], +4 )` / `[ [0xAA10A0], +4 )` | 各 4 |

这些基址都是运行期由 theater 决定的 TileSet 起始号，**具体对应哪些 TileSet 未确认**。

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

## 七、随机数消耗账本（复刻对齐的核心）

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
| 水面细化 `FUN_0059C630` | 每格 1~2 | 已还原并对拍 |

**正态偏差的消耗最不直观**：`NormalDeviate` 一次算两个、缓存第二个，
所以「两次调用」才消耗一轮；而一轮里 Marsaglia 拒绝（落在单位圆外）
会**额外**吃 2 个 `Random()`。每步中心游走正好调两次 → 每步稳定消耗一轮。
详见 [fast-math.md](fast-math.md) §三。

**尚未清点**：`FUN_0057A0C0` 四遍平滑内部是否取随机数（**未确认**，见下）。

## 八、下一轮

1. 啃 `FUN_0057A0C0` 的四遍平滑（决定一块陆地是被接受还是被抹回海里，
   且可能消耗随机数——不查清就无法对齐整条随机数流）
2. 确认 `FUN_0042B1F0`「可用格子总数」的口径（MapType 1/2 的覆盖率分母）
3. 确认工作记录 0x50 字节的完整字段布局（目前只锁定 `+0x38 / +0x3C / +0x4A / +0x4B`）
4. 确认 `0xAA0738` 等 tile 组基址由 theater 决定的具体来源
5. 把上述伪码落成 C++ 并端到端对拍（需要先有地图/格子容器）
