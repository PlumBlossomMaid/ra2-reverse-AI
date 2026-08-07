# 阶段 6：Creating starting points——挑出各家的出生点

> 状态：**外层重试结构、区域筛选与配额分配、五个取数点的惯用法均已实锤**；
> 两个核心子函数 `0x594420 GrowRegion` 与 `0x594F40 ChooseWaypoints`
> 已逐条逆出（§三之一 / §四），三个辅助谓词也已实锤（§五）。
> **尚未落 C++**：它卡在一个大依赖上——引擎的**移动区（zone）子系统**，
> 见 §二之零。
> 取证：`memory/data/decomp/rmg_starting_points.txt`、
> `memory/data/decomp/rmg_start_helpers.txt`（`0x594420` / `0x594F40` /
> `0x578640` / `0x5A1FB0`）。
>
> 这一阶段最要命的不是算法，是**它会整段重来**——失败的那一轮已经消耗掉的随机数
> 不会退回去。复刻时必须把重试循环原样搬过来，否则种子从这里开始就分岔。

## 一、主流程里的重试循环

```c
do {
    do { ok = PlaceStartPoints(); } while (!ok);   // 0x594B50
    ok = ValidateStartPoints();                     // 0x5A1FB0
} while (!ok);
```

- `0x594B50` 的所有出口都 `return 1`（`CONCAT31(uVar10,1)`），**内层循环实际只跑一遍**
- `0x5A1FB0` 会对每个出生点做一次最小堆扩散、量一量可达面积，不满意就返回 0
  → **整个 `0x594B50` 重跑，随机数继续往下消耗**
- `0x5A1FB0` 自己**不取随机数**（它不在取数点清单里）

## 二、`PlaceStartPoints`（`0x594B50`）

### 第 0 步（新查实）：**把阶段 3/4 建的区域全部拆掉，改按「移动区」重新划**

```
00594BA9  call 0x56C510          ; MapClass::RecalculateZones，返回**最大连通区的编号**
00594BAE  mov  ecx, [0xABE014]   ; MapType
00594BB6  mov  ebx, [esp+ecx*4+0x38]        ; zone = zoneSel[MapType]
00594BBA  mov  edx, [ebx*4 + 0x87F800]      ; = MapClass::Instance + 0x18 + zone*4
00594BC1  mov  bp,  [edx + eax*2]           ; want = 该 MovementZone 下、最大连通区的「区类型」
00594BC5  call 0x58CE90                     ; ← DestroyAllRegions
```

`0x58CE90` 做三件事（`0x58CEAE`–`0x58CF79`）：把 `side²` 个工作记录的
`+0x38` / `+0x3C` 全部写回 −1、逐个 `delete` 全局区域向量里的每个 `RegionClass`、
最后 `[0xABED14] = 0`。**也就是说阶段 3 和阶段 4 辛苦切出来的区域到这里全丢了**，
阶段 6 之后所有「区域」都是按移动区重新长出来的（第 2 步）。

`zoneSel` 是栈上的常量表，一共 **5 项**（不是 4 项）：

| MapType | 0 | 1 | 2 | 3 | 4 |
|---------|---|---|---|---|---|
| zone    | 5 | 5 | 5 | 0 | 0 |

（写入点 `0x594B60` / `0x594B6A` / `0x594B6E` 三条 `mov ..., 5`，
`0x594B76` / `0x594B7A` 两条 `mov ..., 0`；表基址就是 `esp+ecx*4+0x38`。）

### ⚠️ 这里是整条流水线唯一一处依赖「引擎移动区子系统」的地方

- `0x56C510 MapClass::RecalculateZones`：先把 `+0x18..+0x48` 那 13 个
  「区编号 → 区类型」表全部 `free`、把 `+0x68` 那张**每格 4 字节**的表
  （`byte0` = 地面类型，`word@+2` = 区编号）的区编号清 0，
  然后从编号 1 开始逐格洪泛（`0x56CB90`），一边记**格数最多的那个区**的编号，
  最后把它返回。
- `0x56D230 MapClass::GetMovementZoneType(coord, zone, flag)`：
  `idx = Y*(MapClass->+0xF8 + MapClass->+0xF4 + 1) + X`（夹到 `[0, +0x6C)`），
  `zoneNum = word[MapClass->+0x68 + idx*4 + 2]`，
  返回 `word[MapClass->(+0x18 + zone*4)[zoneNum]]`。
  `flag` 为真时还会先走一条**桥/隧道**分支（`cell->+0x140 & 0x100`、
  `MapClass->+0x54` 那张 16 字节一项的表），阶段 6 的两处调用 `flag` 都传 0，
  所以那条分支在 RMG 里进不去。

洪泛的连通判据取决于每格的**可通行性**，而可通行性来自砖的 land type、
overlay、`+0x140` 位域……这是一整套引擎子系统，**是复刻阶段 6 的前置工程**，
目前尚未开工。记为本仓库当前最大的一块待办。


### 第 1 步：算「区域太小就不要」的阈值

```
00594B53  fild  dword ptr [0xABE15C]
00594B64  fimul dword ptr [0xABE158]
00594B7E  fmul  qword ptr [0x7ED8D0]     ; x 0.03
00594B84  fld   qword ptr [0x7ED8C8]     ; 400.0
00594B8A  fcomp st(1)                    ; 取两者较大者
00594B9B  call  0x7C5F00                 ; _ftol（截断）
```

```c
int minCells = (int)max((double)([0xABE15C] * [0xABE158]) * 0.03, 400.0);
```

即「面积的 3%，但至少 400 格」。`[0xABE158]` / `[0xABE15C]` 就是
`MapSeedClass + 0x180` / `+0x184` = **地图真实宽 W / 高 H**
（见 [stage-init-regions.md](stage-init-regions.md) §六；同一对字段在
`0x5A0700` 里也是拿来当「整张图的格子数」预留容量的）。所以这里是 `H * W * 0.03`。

### 第 2 步：按移动区重新划区域

```c
// zone 与 want 在第 0 步已经算好
for (Cell* c : MapCells) {                        // 迭代器序 0x578350 / 0x578290
    if (!InDiamond(&c->MapCoords))                continue;   // 0x5AC230
    if (work[c].regionId != -1)                   continue;   // +0x38
    if (c->+0x4C != 0)                            continue;
    if (MapClass::GetMovementZoneType(&c->MapCoords, zone, 0) != want) continue;
    GrowRegion(c, zone, want);                    // 0x594420
}
```

然后把 `cellCount`（`+0x0C`）小于 `minCells` 的区域全部销毁。

### 第 2 步之一：`GrowRegion`（`0x594420`）——不取随机数

和阶段 3 的 `SeedRegion` 是同一个骨架，只是连通判据换成了「移动区类型相同」：

```c
void GrowRegion(Cell* seed, int zone, int want) {
    Region* r = new RegionClass(seed->MapCoords);   // 0x58BF70，编号取 [0xABED14] 并 +1
    int id = r->id;                                 // +0x08

    Vector<CellStruct> stack;                       // 局部，vtable 0x7E3890，增长步长 20000
    work[seed].queued = id;                         // +0x3C，**先占位**
    stack.push_back(seed->MapCoords);

    while (!stack.empty()) {
        CellStruct xy = stack.pop_back();           // 后进先出 = DFS
        work[xy].regionId = id;                     // +0x38
        r->cellCount++;                             // +0x0C
        r->cells.push_back(xy);                     // +0x28 那个向量
        for (int d = 0; d < 8; ++d) {               // 八邻域，表 0x89F688
            Cell* n = TryGetCellAt(xy + AdjacentCell[d]);   // ⚠️ 先取格再判界，写哑元 +0x24
            if (!InDiamond(&n->MapCoords))                     continue;
            if (n->+0x4C != 0)                                 continue;
            if (GetMovementZoneType(&n->MapCoords, zone, 0) != want) continue;
            if (work[n].queued != -1)                          continue;   // +0x3C
            work[n].queued = id;
            stack.push_back(n->MapCoords);
        }
    }
}
```

两处要照抄的：**先取格再判界**（越界邻格会往共享哑元 `[0xABDC74]` 写一次坐标，
和阶段 3 的 `0x58D410` 同款副作用）；以及 `+0x3C` 只写不清，
所以每格最多入栈一次。

### 第 3 步：排序

给每个存活区域算一个排序键，再 `qsort`（`0x7C8B48`，比较函数 `0x5AD9C0`）：

```c
key = (500000 - region->cellCount) * regionCount + region->regionId;
```

`500000 - cellCount` 让**大区域排在前面**；乘 `regionCount` 再加 `regionId`
是把「编号」当低位，保证同样大小的区域按编号稳定排序。

### 第 4 步：把出生点名额按面积比例分给各区域

```c
int total = [0xABE028];        // 出生点总数
int acc = 0, assigned = 0;
for (int i = 0; i < n; ++i) {
    acc += list[i]->cellCount;
    int give;
    if (i == n - 1) give = total - assigned;                  // 最后一个吃余数
    else            give = (int)((double)acc / allCells * total + 0.5) - assigned;
    list[i]->startCount = give;                               // +0x20
    assigned += give;
}
```

对应汇编（`0x594E25`–`0x594E40`）：

```
00594E25  fild  dword ptr [esp+0x1C]      ; acc（累计格子数）
00594E29  fidiv dword ptr [esp+0x10]      ; / 全部区域格子数
00594E2D  fimul dword ptr [esp+0x14]      ; * total
00594E31  fadd  qword ptr [0x7E1738]      ; + 0.5
00594E37  fisub dword ptr [esp+0x18]      ; - 已分配
00594E3B  call  0x7C5F00                  ; _ftol
00594E40  mov   [edi+0x20], eax
```

`fadd 0.5` 之后走 `_ftol` 截断，等价于对**累计比例**做四舍五入再作差——
是标准的「累计取整」分配法，不会因为逐个四舍五入而丢名额。

⚠ 注意 `fadd 0.5` 在 `fisub` **之前**：先给累计值加 0.5 再减已分配数，
和「(比例 - 已分配) + 0.5」在浮点上不是同一个表达式，照抄顺序。

### 第 5 步：在每个区域里放点

```c
int base = 0;
for (auto* r : list) { PickPointsInRegion(r, base); base += r->startCount; }   // 0x594870
```

### 第 6 步：给空手的区域补个建筑（**取数点 `0x594EA1`**）

```c
if ([0xABE014] == 0)
    for (auto* r : list)
        if (r->startCount == 0 && UniformUnit() < 0.5)
            PlaceRandomBuilding(r);        // 0x595400
```

汇编实锤是严格小于：

```
00594EA1  mov   ecx, 0xABE890
00594EA6  call  Randomizer::Random
00594EB7  fild  qword ptr [esp+0x1C]       ; (double)(uint32)r
00594EBB  fmul  qword ptr [0x7ED898]       ; x 1/(2^32-1)
00594EC1  fcomp qword ptr [0x7E1738]       ; 与 0.5 比
00594EC9  test  ah, 1                      ; C0：ST(0) < 0.5
00594ECC  je    跳过
```

## 三、`PickPointsInRegion`（`0x594870`）——两个取数点

```c
// 取数点 1：0x5948CD —— PickBelow(3)，比例常数 0x7ED8C0 = 3/(2^32-1)
int extra;
do { extra = (int)((double)(uint32)Random() * (3.0/4294967295.0)); } while (extra > 2);

int budget = extra + region->startCount * 15;
Vector<CellStruct> cand;
for (int tries = 0; cand.size() < budget && tries < 300; ++tries) {
    // 取数点 2：0x594935 —— PickBelow(region->cellCount)，标准 span x (1/(2^32-1))
    int n = region->cells.Count;                       // +0x38
    int k; do { k = (int)((double)(uint32)Random() * (double)n * (1.0/4294967295.0)); }
           while (k > n - 1);
    CellStruct xy = region->cells[k];                  // +0x2C

    Rect box = { xy.X - 3, xy.Y - 3, 6, 6 };
    if (Pred_0x5A7250(...) && Rect_0x578640(&usableArea, &xy, 1))
        cand.push_back(xy);
}
ChooseWaypoints(cand);                                 // 0x594F40，不取随机数（§四）
```

三个要点：

1. **`extra ∈ {0,1,2}` 是一次独立抽取**，而且**无论 `startCount` 是不是 0 都要抽**
   ——`startCount == 0` 的区域预算是 `extra + 0`，循环可能一次都不进，
   但那一个随机数已经花掉了
2. 候选采样是**有放回**的，`tries` 上限 300 与预算上限同时生效
3. `usableArea` 是地图可用矩形往里缩：`(x+4, y+4, w-8, h-8)`，
   四个分量取自 `[0x87F8E4]`/`[0x87F8E8]`/`[0x87F8EC]`/`[0x87F8F0]`

选出的坐标写进 waypoint 表，并给格子打上 `+0x140 |= 4`。

## 四、`ChooseWaypoints`（`0x594F40`）——最远点采样，一个随机数都不取

`PickPointsInRegion` 攒完候选表就交给它。它**不抽签**，是纯确定性的
「最远点采样（farthest-point sampling）」：先挑一对彼此最远的候选取其一，
再一个一个往外挑「离已选点集最远」的那个，挑够为止。

### 要挑几个

```
00594F49  fild  dword ptr [0xABE030]          ; MapSeedClass + 0x58（语义未确认）
00594F55  fmul  qword ptr [0x7E3808]          ; x 0.01
00594F77  fmul  qword ptr [0x7ED8D8]          ; x 12.0
00594F7D  fidiv dword ptr [0xABE028]          ; / NumPlayers
00594F83  fadd  qword ptr [0x7E1708]          ; + 2.0
00594F89  fimul dword ptr [esp+0x24]          ; x region->startCount（+0x20）
00594F8D  call  0x7C5F00                      ; _ftol（截断）
```

```c
int want = (int)((MapSeed->+0x58 * 0.01 * 12.0 / NumPlayers + 2.0) * region->startCount);

if (region->startCount == 0 && cand.Count > 0) want = cand.Count;
if (want > cand.Count || want == 0) {
    if (region->startCount == 0) return NULL;      // 空手区域直接放弃
    want = cand.Count;
}
```

⚠️ **两处溢出式的兜底**：`startCount == 0` 时 `want` 先被强行改成 `cand.Count`；
接着那条 `want > Count || want == 0` 又会**再改一次**。照抄顺序。

### 第一个点：全体两两配对里得分最高的那个 `i`

```c
double best = -1.0; int bestI = -1;
for (int i = 0; i < cand.Count - 1; ++i) {
    int ra = work[cand[i]].regionId;               // +0x38，工作数组为空时取 -1
    for (int j = i + 1; j < cand.Count; ++j) {
        int rb = work[cand[j]].regionId;
        int dx = cand[i].X - cand[j].X, dy = cand[i].Y - cand[j].Y;
        double score = FastSqrt((double)(dy*dy + dx*dx)) + (ra != rb ? 20 : 0);
        if (score > best) { best = score; bestI = i; }     // 严格大于
    }
}
if (bestI != -1) { out.push_back(cand[bestI]); goto 补点; }
// bestI == -1（候选不足 2 个）
if (cand.Count != 0) { out.push_back(cand[0]); return out; }   // ⚠️ 直接返回，不管 want
```

`+20` 那一笔是 `sub / neg / sbb / and 0x14` 的无分支写法：
**两个候选不属于同一个区域时白送 20 格的距离**。

### 后续的点：最大化「到已选点集的最小距离」

```c
补点:
while (out.Count < want) {
    double best = -1.0; int bestI = -1;
    for (int i = 0; i < cand.Count; ++i) {
        int ra = work[cand[i]].regionId;
        double mn = 9999999.0;                     // 0x416312CFE0000000，不是 1e7
        for (int j = 0; j < out.Count; ++j) {
            int rb = work[out[j]].regionId;
            int dx = cand[i].X - out[j].X, dy = cand[i].Y - out[j].Y;
            double d = FastSqrt((double)(dy*dy + dx*dx)) + (rb != ra ? 20 : 0);
            if (d < mn) mn = d;                    // 严格小于
        }
        if (mn > best) { best = mn; bestI = i; }   // 严格大于
    }
    out.push_back(cand[bestI]);
}
```

⚠️ **候选被选中之后不会从候选表里删掉**。它到已选集合的最小距离会变成 `0`，
而 `best` 的初值是 `-1.0`，所以只要还有名额、`0 > -1` 就成立——
**同一个格子可以被选进 waypoint 表两次**。属于「照抄，不要修」的一类。

⚠️ `-1.0` 与 `9999999.0` 两个哨兵都是**直接砌在指令流里的 64 位立即数**
（`mov dword [esp+..], 0xBFF00000` 之类），不是 `.rdata` 常量。
后者精确等于 `9999999.0`（`0x416312CFE0000000`），**不是 `1e7`**
（`1e7 == 0x416312D000000000`，两者差整整 1.0）——源码里多半写的就是 `9999999`。

## 五、`PlaceRandomBuilding`（`0x595400`）——两个取数点，阶段 6 / 7 共用

```c
// 取数点 1：0x595461 —— 从规则表里随机挑一种建筑
int n = [[0x8871E0] + 0xAEC];                      // 列表长度
int k; do { k = PickBelow_inline(n); } while (k > n - 1);
Building* b = new BuildingClass([[0x8871E0] + 0xAE0][k], owner);

for (int attempt = 0; attempt < 100; ++attempt) {
    // 取数点 2：0x5954DA —— 从区域格子里随机挑一格
    int m = region->cells.Count;                   // +0x38
    int j; do { j = PickBelow_inline(m); } while (j > m - 1);
    CellStruct at = region->cells[j];              // +0x2C

    // 逐格检查建筑占地（占地表由建筑类型的虚函数 +0x90 给出，以 (0x7FFF,0x7FFF) 收尾）
    bool ok = true;
    for (each (dx,dy) in footprint) {
        Cell* c = TryGetCellAt(at + (dx,dy));
        if (c->+0xE4 != 0)                    { ok = false; break; }  // 已有占用者
        if (!IsPlain(c))                      { ok = false; break; }  // 0x486380
        if (c->Level != at.Level)             { ok = false; break; }  // +0x11B 必须同层
        if (c->+0xEC == 3)                    { ok = false; break; }
        if (!IsWithinUsableArea(c, 1))        { ok = false; break; }
        if (work[c].+0x45 != 0)               { ok = false; break; }
    }
    if (ok) { 放下建筑（虚函数 +0xD8，坐标 = (X*256+128, Y*256+128)）; break; }
}
```

⚠️ **建筑种类只抽一次，位置最多抽 100 次**——抽不到位置就把建筑对象丢掉，
但那 100 个随机数已经消耗了。

`0x8871E0` = `RulesClass::Instance`，`+0xADC` 是 `DynamicVectorClass<BuildingTypeClass*>`
（数据 `+0xAE0`、个数 `+0xAEC`），键名已实锤为 **`rules.ini [General] NeutralTechBuildings=`**
——取证见 [stage-tech-buildings.md](stage-tech-buildings.md) §五。

## 六、顺带补上的 RegionClass 字段

阶段 3 那份表里标「未确认」的两项，在这里定了：

| 偏移 | 语义 | 证据 |
|------|------|------|
| `+0x20` | 分到的出生点个数 | `0x594E40` 写、`0x5948F3` / `0x594E9A` 读 |
| `+0x28` | `DynamicVectorClass<CellStruct>`：本区域的格子表 | 数据 `+0x2C`、个数 `+0x38`，与 `0x5A0700` 里那个 0x18 字节向量布局一致 |

顺带把 `DynamicVectorClass` 的布局也钉死（0x18 字节）：

| 偏移 | 内容 |
|------|------|
| `+0x00` | vtable |
| `+0x04` | 数据指针 |
| `+0x08` | 容量 |
| `+0x0C` | （未确认） |
| `+0x10` | 元素个数 |
| `+0x14` | 扩容步长 |

## 七、三个辅助谓词（全部实锤）

| 地址 | 语义 |
|------|------|
| `0x5AC230(CellStruct*)` | `InDiamond` —— 就是 `[0xABED04]` / `[0xABED08]` 那四个不等式的独立封装：`X+Y > W`、`X−Y < W`、`Y−X < W`、`X+Y <= W+2H` |
| `0x5A7250(Rect*, dl, 栈参)` | 「这块矩形是可跨的地面」，见 [stage-making-regions.md](stage-making-regions.md) §八。阶段 6 传的矩形是 `{X−3, Y−3, 6, 6}`，两个开关都是 0 |
| `0x578640(Rect*, CellStruct*, flag)` | `MapClass::IsWithinUsableArea` 的**带矩形**版本，见下 |

### `0x578640` 展开

```c
bool IsWithinUsableArea(const Rect& r, const CellStruct& c, bool considerRamp) {
    int W = MapClass->+0xF4;
    int adj = 0;
    if (considerRamp) {
        Cell* cell = TryGetCellAt(&c);            // ⚠️ 越界写哑元 [0xABDC74]
        adj = (int8)cell->+0x11B;                 // Level，**无条件**取
        if (cell->+0x11C != 0)                    // 有坡道
            if (c.X + c.Y < W + r.y*2 + 4 + adj) ++adj;
    }
    if (c.X + c.Y <= W + r.y*2 + adj)              return false;
    if (c.X + c.Y >  W + (r.y + r.h)*2 + 2 + adj)  return false;
    if (c.X - c.Y >= (r.x + r.w)*2 - W)            return false;
    if (c.Y - c.X >= W - r.x*2)                    return false;
    return true;
}
```

`considerRamp == false` 时 `adj` 恒为 0，四条式子退化成「菱形，但上下边界按
`r.y` / `r.h` 收缩、左右边界按 `r.x` / `r.w` 收缩」——就是把整张图的菱形
按矩形裁一刀。阶段 6 传的 `flag` 是 **1**，所以坡道那条会生效。

`[0x87F8E4] / [0x87F8E8] / [0x87F8EC] / [0x87F8F0]` 分别是
`MapClass::Instance + 0xFC / 0x100 / 0x104 / 0x108`（`MapClass::Instance = 0x87F7E8`），
也就是引擎那份「地图局部矩形」的 x / y / w / h。阶段 6 用的是它**往里缩一圈**的版本
`{x+4, y+4, w−8, h−8}`。

## 八、未确认

1. ~~`[0xABE014]`——`zoneSel[]` 的下标~~ —— **已关闭**：`= MapSeedClass + 0x3C = MapType`
   （表其实有 5 项 `{5,5,5,0,0}`，见 §二之零）
2. ~~`[0xABE028]`——出生点总数~~ —— **已关闭**：`= MapSeedClass + 0x50 = NumPlayers`
3. ~~`[0xABE158]` / `[0xABE15C]`~~ —— **已关闭**：`= MapSeedClass + 0x180 / +0x184`
   = 地图真实宽 W / 高 H
4. ~~`0x5AC230` / `0x594420` / `0x5A7250` / `0x578640` / `0x594F40` 的语义~~
   —— **全部关闭**，见 §二之一（`0x594420`）、§四（`0x594F40`）、§七（其余三个）
4b. **`[0xABE030]`（`MapSeedClass + 0x58`）语义未确认**：全镜像只有 `0x594F4B`
   一处读、找不到写点（`xref_abs` 只报 1 处引用），多半由 RMG 对话框
   通过 `mov [ecx+0x58], ...` 写入。它决定每个区域要摆几个 waypoint
4c. **移动区子系统**（`0x56C510` / `0x56CB90` / `0x56D230`）尚未逆向——
   这是复刻阶段 6 的前置工程，见 §二之零
5. `0x5A1FB0` 判定「这组出生点合格」的具体标准——它做的是最小堆扩散，
   但接受/拒绝的门槛还没读
7. 工作记录 `+0x45`（建筑放置时要求为 0）的含义
