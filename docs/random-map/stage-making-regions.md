# 阶段 4：Making regions——区域重建 + 区域之间开通路

> 状态：**45 个取数点已关掉 37 个**。阶段骨架、末段三个函数、区域切分阈值、
> 开坡道外壳（`0x590970` + `0x590FD0` + `0x5A1E50`）、
> 以及架桥 `0x58F2C0` 已实锤；
> 仍欠区域切分 `0x58D620` 的后半段、
> 以及七个坡道建造函数的砖号几何（见 §十一）。
> 取证：`memory/data/decomp/rmg_regions_stage4{,b,c}.txt` /
> `rmg_ramp_stage4{d,e,f}.txt` / `rmg_bridge_stage4{g,h,i}.txt`。
>
> 这是全流水线**取数最密的一段（45 处）**，而且**只在 `MapType ∈ {3,4}` 下执行**——
> 与 [stage-water-maptype34.md](stage-water-maptype34.md) 是同一条支路。
> 名字叫 "Making regions"，但阶段 3 才是"切区域"；这一阶段真正在做的是
> **把阶段 3 切出来的区域连起来**：造坡道、架桥、修悬崖。

## 一、它在流水线里的样子

```c
FUN_004068e0("RMG: Making regions");
if (MapType == 4 || MapType == 3) {
    FUN_0058ebc0();          // 重算区域成员/包围盒；过大的区域切开
    FUN_0058ef10();          // 推倒重建区域，再算邻区表、开通路
    FUN_005a19e0();          // 悬崖直角处随机削一格 / 垫一格
    FUN_00578e60(0, -1);     // 全图重刷砖（坡道、LAT 边界）
    FUN_005a17f0();          // 多格悬崖砖重掷同形变体
}
```

五个调用**都不带条件**，但外层那一句 `MapType == 4 || MapType == 3` 让整段在
默认参数（`MapType = 1`）下**一次也不跑**。

## 二、`0x58EBC0`：重算成员 + 切开过大的区域

### 第一遍：清空每个区域的成员表与包围盒

```c
for (Region* r : regions) {
    r->cells.Clear();          // 虚表 +0x28 的 +0x0C
    r->cellCount = 0;          // +0x0C
    r->bbox = { 9999, 9999, 0, 0 };   // +0x40 X / +0x44 Y / +0x48 W / +0x4C H
}
```

### 第二遍：**倒着**扫工作数组，把格子塞回各自的区域

```c
for (int i = side*side - 1; i >= 0; --i) {       // ← 下标从大到小
    CellStruct xy = work[i].coord;               // +0x00
    if (!InDiamond(xy)) continue;                // 0xABED04 / 0xABED08 四不等式
    int id = work[i].regionId;                   // +0x38
    if (id < 0 || id >= regionCount) continue;
    Region* r = regions[id];
    r->cellCount++;
    r->cells.AddItem(xy);                        // 内联 AddItem，含扩容
    GrowRect(&r->bbox, xy);                      // 见下
}
```

⚠️ **倒序**这一条必须照抄：`region->cells[k]` 的顺序直接决定 `0x58D620` 抽种子格
时抽中谁。正着扫会得到镜像的种子分布。

包围盒的生长写法（`0x58EC9D`–`0x58ED38`）用 `W == 0` 当"还没初始化"的标志，
而不是另设一个 bool：

```c
if (r->bbox.W == 0) { r->bbox = { x, y, 1, 1 }; }
if (x >= r->bbox.X + r->bbox.W) r->bbox.W = x - r->bbox.X + 1;
if (x <  r->bbox.X)           { r->bbox.W += r->bbox.X - x; r->bbox.X = x; }
if (y >= r->bbox.Y + r->bbox.H) r->bbox.H = y - r->bbox.Y + 1;
if (y <  r->bbox.Y)           { r->bbox.H += r->bbox.Y - y; r->bbox.Y = y; }
```

（第一遍把 X/Y 预置成 9999、W/H 预置成 0，所以只要有一格落进来就会被那条
`W == 0` 分支重置成真实坐标，9999 不会残留。）

### 第三遍：区域太大就切开

阈值是**一次性算好的一个整数**（`0x58ED89`–`0x58EDA9`）：

```asm
0058ED89  fild  dword ptr [0xABE048]      ; (double)MapSeedClass->RegionSize   (+0x70)
0058ED8F  fmul  qword ptr [0x7E44E8]      ; × 0.005
0058ED95  fadd  qword ptr [0x7E8AE8]      ; + 0.05
0058ED9B  fimul dword ptr [0xABE15C]      ; × (int)MapSeedClass->+0x184  = 地图高 H
0058EDA1  fimul dword ptr [0xABE158]      ; × (int)MapSeedClass->+0x180  = 地图宽 W
0058EDA7  fadd  st(0), st(0)              ; × 2
0058EDA9  call  0x7C5F00                  ; _ftol
```

即

> `maxCells = _ftol( 2 · ((RegionSize · 0.005 + 0.05) · H) · W )`

乘的顺序（先 `×H` 再 `×W` 最后自加一倍）照抄，别化简成 `2·k·W·H`。
`RegionSize` 默认 0 ⇒ 阈值 = `0.1 · 地图面积`。

> **顺带关掉一个旧的未确认项。** [stage-init-regions.md](stage-init-regions.md) §六
> 第 4 条问「`0x5A0700` 的容量预留 `[MapSeed+0x180] * [MapSeed+0x184]`
> 对应哪两个 rules 项」——答案是**它俩根本不是规则项**，就是
> [stage-init.md](stage-init.md) 算出来的**真实地图宽 W 与高 H**
> （写入点 `0x599700` / `0x599748`）。那处预留的是"整张图的格子数"。

扫描循环本身带**重启**语义：

```c
restart:
for (int i = 0; i < regionCount; ++i) {
    Region* r = regions[i];
    if (r->doneSplitting /*+0x1A*/ != 0) continue;
    if (r->isWater      /*+0x14*/ != 0) continue;
    if (r->cellCount > maxCells) {
        if (Split(r) /*0x58D620*/) { DestroyRegion(r); delete r; goto restart; }
    } else {
        r->doneSplitting = 1;          // 够小了，以后不再考虑
    }
}
```

于是 `+0x1A` 的语义可以定下来：**「这个区域已经确定不用再切了」**。
（构造时是 0，见 [stage-init-regions.md](stage-init-regions.md) §五。）

`DestroyRegion` 那段是 `0x58C070` 的**内联展开**——从全局向量里 `RemoveItem`
（找下标 → 手写 memmove 前移 → `count--`）、还掉成员表的缓冲区、再 `delete`。

## 三、`0x58EF10`：推倒重建，然后开通路

```c
bool ok = true;
DisplayClass::SetActiveFoundation(0);
for (Cell* c : MapCells) { if (!ok) break; ok = FUN_00579010(c, -1); }   // 重刷砖

for (int i = 0; i < side*side; ++i) { work[i].regionId = -1; work[i].queued = -1; }
for (int i = regionCount - 1; i >= 0; --i) { DestroyRegion(regions[i]); delete regions[i]; }
regionCounter /*0xABED14*/ = 0;

// 重新按连通块建区域，和阶段 3 的 CreateRegions 一模一样
for (int i = 0; i < side*side; ++i) {
    if (work[i].regionId != -1) continue;
    CellStruct zero = {0, 0};
    if (!NotEqual(&work[i].coord, &zero)) continue;      // 0x5AC370：还是那条 (0,0) 哨兵
    Cell* c = MapClass::TryGetCellAt(&work[i].coord);    // 0x5657A0
    Region* r = SeedRegion(c);                           // 0x58C800
    if (r) r->doneSplitting /*+0x1A*/ = 0;
}

if (ok) {
    for (Region* r : regions) BuildNeighbourList(r);     // 0x58F0C0
    for (Region* r : regions) MakeConnections(r);        // 0x5905D0
    for (Region* r : regions) { delete r->neighbours; r->neighbours = 0; }   // +0x04
}
return 1;
```

⚠️ 注意 `SeedRegion`（`0x58C800`）**自己也取随机数**（`0x58CAF9`，与阶段 3 共用
的那个取数点），所以"重建"这一步不是零成本的。

`0x5AC370` 就是两个 `CellStruct` 的**不等**比较（相等返回 0）——阶段 3 那条
「左上角 (0,0) 被无条件跳过」的怪癖在这里原样再来一遍。

## 四、`0x58F0C0`：算每个区域的邻区表（不取随机数）

```c
r->neighbours /*+0x04*/ = new DynamicVectorClass<int>(growth = 10);   // vtable 0x7E4E78
char* seen = new char[regionCounter]{0};                              // 0xABED14

for (CellStruct b : GetBorderCells(r) /*0x58D410*/)
    for (int d = 0; d < 8; ++d) {
        CellStruct n = b + AdjacentCell[d];
        if (!InDiamond(n)) continue;
        int id = work[n].regionId;
        if (id >= 0) seen[id] = 1;
    }

for (int id = 0; id < regionCounter; ++id)
    if (seen[id] && id != r->id /*+0x08*/) r->neighbours.AddItem(id);

// 顺手重数一遍自己的格子（这次连 (0,0) 哨兵一起排除）
r->cellCount = 0;
for (int i = 0; i < side*side; ++i)
    if (work[i].regionId == r->id && work[i].coord != (0,0)) r->cellCount++;
```

`seen` 按 `regionCounter`（**历史上建过多少个区域**，`0xABED14`）分配，
而不是按当前的 `regionCount`（`0xABDFA0`）——两者在切分/合并之后不相等，
这里用的是前者，因为 `regionId` 是历史编号。

邻区表是**另一个** `DynamicVectorClass` 实例（`0x18` 字节，虚表 `0x7E4E78`），
字段布局与 RegionClass `+0x28` 内嵌的那个不同，落 C++ 时别混：

| 偏移 | 内容 |
|---|---|
| `+0x00` | 虚表 |
| `+0x04` | 数据指针 |
| `+0x08` | 容量 |
| `+0x0D` | IsAllocated |
| `+0x10` | 个数 |
| `+0x14` | 增长步长（这里是 10） |

## 五、`0x5905D0`：开通路——`Accessibility` 就在这里生效

分成互斥的两半，看这个区域是水还是陆（`+0x14`）。

### 陆地区域：给每一对高度不同的相邻区域开坡道

```c
for (int k = 0; k < r->neighbours.count; ++k) {
    Region* nb = FindRegionById(r->neighbours[k]);      // 线性查 +0x08
    if (nb->id <= r->id) continue;                      // 每对只处理一次
    if (nb->level == r->level) continue;                // 同层不用坡道

    int roll  = PickBelow(101);                         // 0x590792，取 0..100
    int extra = (roll < MapSeedClass->Accessibility /*+0x6C，全局 0xABE044*/)
                    ? PickBelow(3)                      // 0x5907C0
                    : 0;
    int want  = extra + 1;                              // 要开 1 ~ 3 条

    Region* hi = (r->level < nb->level) ? nb : r;       // 层高大的那个
    int     lo = (r->level < nb->level) ? r->id : nb->id;
    Vector<CellStruct> border = GetBorderCells(hi);     // 0x58D410

    int made = 0, tries = 0;
    do {                                                // 注意是 do-while
        if (tries > 99) break;                          // 100 次上限
        CellStruct c = border[PickBelow(border.count)]; // 0x590889
        if (MapClass::IsWithinUsableArea(&c, 1))
            made += (FUN_00590970(&c, lo, (float)tries * 0.01f) != 0);   // 0x7EAAE0
        ++tries;
    } while (made < want);
    if (made == 0) r->+0x1B = 0;
}
```

四条要点：

1. **`Accessibility` 的确切语义**：`0xABE044 = MapSeedClass + 0x6C`。
   `PickBelow(101) < Accessibility` 的概率就是 `Accessibility/101`
   （`Accessibility ≤ 0` 时恒不成立，`≥ 101` 时恒成立）。命中就再抽一次
   `PickBelow(3)`，于是每对邻区要开的坡道条数是 `1`（未命中）或 `1..3`（命中）。
   **抽第一个数是无条件的**——`Accessibility = 0` 也照抽，随机数流照走；
2. 第三个参数 `tries * 0.01f` 随重试次数线性增大，形状上像是**放宽某个角度/容差**，
   具体含义要等 `0x590970` 逆完才能说，**现在不下结论**；
3. `+0x1B` 在一条坡道都没开成时被清 0。结合构造时的初值 1，
   它**很可能**是"这个区域连得上外面吗"的标记——**未确认**，
   因为还没找到读它的地方；
4. **边界格为空时会越界读。** 抽格子那段是标准拒绝采样的内联展开，
   上界写成 `hi = count − 1` 再 `while (draw > hi) 重抽`。`count == 0` 时
   `hi` 是无符号的 `0xFFFFFFFF`，条件恒不成立 ⇒ **第一次抽到什么就用什么**，
   然后拿去索引空数组。是否真能触发取决于 `GetBorderCells` 会不会返回空表
   （**未确认**）。旁边还有一句 `(double)(ulonglong)count` 算完就丢——
   多半是作者原本想用浮点比例法、改成拒绝采样后没删干净。

### 水域区域：让隔水相望的两个陆地邻区连起来

```c
if (r->neighbours.count < 2) return;                     // 写成 count != 1 && count-1 >= 0
for (int i = 1; i < r->neighbours.count; ++i) {          // 出口判据在循环尾，见下
    Region* a = FindRegionById(r->neighbours[i - 1]);
    if (!(a->neighbours.count > 1 || a->cellCount > 0x32)) continue;
    for (int j = i; j < r->neighbours.count; ++j) {      // 每轮重读 count
        Region* b = FindRegionById(r->neighbours[j]);
        if (!(b->neighbours.count > 1 || b->cellCount > 0x32)) continue;
        if (b->isWater) continue;
        if (b->level != a->level || a->level != r->level) continue;
        FUN_0058f2c0(a, b);                              // 5 个取数点
    }
}
```

`a` 那一侧**没有**查 `isWater`，只有 `b` 查了；`a` 的层高是靠
`b->level == a->level && a->level == r->level` 间接约束的。三处 `count`
都在循环里重新从 `r->neighbours` 读，说明作者预期 `0x58F2C0` 可能改动区域表。

`0x58F2C0` **就是架桥**——由水区域出面、把两个同层的陆地邻区配对、
两边都得"够大或够连通"（`邻区数 > 1 或 格子数 > 50`），然后在水面上铺一座低桥。
逐条取证见 §五之三。

> 上一版这里写的是"**架桥**是首选假说，刻意不写成架桥"。现在假说升级成了结论：
> 桥面砖号解出来是 `LOBRDG01..25`、桥头用的正好是 `PavedRoadEnds` 的 4 个成员、
> 桥上还会盖一间 `CABHUT`（桥梁维修小屋）。三条独立证据，全部落在 exe 字节上。

> 反编译里外层下标是 `+= 4`（字节偏移，强度削减的产物）而内层是 `* 4`（元素下标）。
> 两种写法**语义相同**——一度以为外层恒取 `neighbours[0]`，
> 回读伪代码 `*(int *)(data + local_3c)` 才确认 `local_3c` 本来就是字节偏移。
> 记在这里当反例：**编译器的强度削减会把同一份逻辑写成两副面孔。**

## 五之二、`0x590970`：真正在开坡道——8 个取数点全部落地

签名：`char BuildRamp(CellStruct* cell, int targetRegionId, float tol)`。
`tol` 就是 §五传进来的 `tries × 0.01f`，**它有两处独立的作用**，
下面两小节各说一处。

### 邻域探测 `0x590FD0`：把 15×15 压成一个八方向掩码

```c
uint Probe(CellStruct* cell, char* ok, int regionId, float tol) {
    int need = _ftol((15.0 - 5.0) * (1.0f - tol) + 5.0);   // 见下
    Rect c = { cell->X - 2, cell->Y - 2, 5, 5 };
    if (!CountAtLeast(&c, regionId, need)) { *ok = 0; return 0; }   // 0x5A1E50
    uint mask = 0;
    for (int i = 0; i < 3; ++i)          // i 走 Y
      for (int j = 0; j < 3; ++j) {      // j 走 X
        if (i == 1 && j == 1) continue;  // 中心格刚才单独测过
        Rect r = { cell->X - 7 + 5*j, cell->Y - 7 + 5*i, 5, 5 };
        if (CountAtLeast(&r, regionId, need)) mask |= kDirBit[j + 3*i];
      }
    return mask;
}
```

`0x5A1E50(Rect*, regionId, need)` 数这个矩形里有几格 `work[].regionId == regionId`，
**够 `need` 格就立刻返回 true**（提前退出，不数完）：

```c
bool CountAtLeast(Rect* r, int id, int need) {
    int hit = 0;
    for (int dy = 0; dy < r->H; ++dy)
      for (int dx = 0; dx < r->W; ++dx) {
        short x = r->X + dx, y = r->Y + dy;
        if (!InDiamond(x, y)) continue;                 // 0xABED04 / 0xABED08
        int owner = work ? work[y*side + x].regionId : -1;
        if (owner == id && ++hit >= need) return true;
      }
    return false;
}
```

于是 3×3 个 5×5 的盒子正好铺满以本格为中心的 **15×15**：中心那个必须过
（不过就整个函数放弃、`*ok = 0`），周围八个各自过了就点亮自己那一位。

**`tol` 的第一处作用：它是「盒子里要有多少格属于目标区域」的线性放宽。**

```asm
00590FD3  fld   qword [0x7ED6E0]   ; 15.0
00590FD9  fsub  qword [0x7ED6D8]   ; − 5.0        → 10.0
00590FDF  fld   dword [0x7E2AC8]   ; 1.0f
00590FE5  fsub  dword [esp+0x28]   ; − tol
00590FEB  fmulp st(1)              ; 10.0 × (1 − tol)
00590FED  fadd  qword [0x7ED6D8]   ; + 5.0
00590FF5  call  0x7C5F00           ; _ftol
```

即 `need = _ftol(lerp(5, 15, 1 − tol))`：第 0 次尝试要 **15/25 = 60%**，
第 99 次只要 **5/25 = 20%**。写成 `(hi−lo)·t + lo` 而不是 `lo + (hi−lo)·t`
的顺序照抄——两者在浮点上不等价。

### 方向位表 `0x82AF18`：位↔方向的**数据级**佐证

`kDirBit` 是 9 个 dword（不是 byte，Ghidra 猜错了类型）：

| | `j=0`（X−5） | `j=1`（X） | `j=2`（X+5） |
|---|---|---|---|
| `i=0`（Y−5） | `0x40` NW | `0x80` N | `0x01` NE |
| `i=1`（Y） | `0x20` W | `0x00` — | `0x02` E |
| `i=2`（Y+5） | `0x10` SW | `0x08` S | `0x04` SE |

这张表把 §九那条「`bit k ⟺ AdjacentCell[(k+1)&7]`」从**代码推断**
升级成了**数据实证**——`0x57B210`（水掩码）、`0x579B70`（高差掩码）、
`0x590FD0`（区域掩码）三处用的是同一套编码，而这一处是明写在 `.data` 里的。

### 八条分支：四个转角 + 四个直坡

掩码到手之后是八个**顺序尝试、一成功就跳出**的分支。前四条要求
"两个相邻方向有、对面三个方向没有"，是转角；后四条要求
"一整边没有、且南北（或东西）至少一侧有"，是直坡：

| 判据 | 含义 | 建造函数 | 取数 |
|---|---|---|---|
| `m & 0x82 == 0x82 && m & 0x38 == 0` | N+E 有，S/SW/W 无 | `0x593AF0` | 0 |
| `m & 0x0A == 0x0A && m & 0x11 == 0` | E+S 有，NE/SW 无 | `0x593550` | 0 |
| `m & 0x28 == 0x28 && m & 0x83 == 0` | S+W 有，N/NE/E 无 | `0x593030` | 0 |
| `m & 0xA0 == 0xA0 && m & 0x0E == 0` | W+N 有，E/SE/S 无 | `0x593030` | 0 |
| `m & 0x70 == 0 && m & 0x88 != 0` | 西侧三格全无，N 或 S 有 | `0x592440` | **2** |
| `m & 0x07 == 0 && m & 0x88 != 0` | 东侧三格全无，N 或 S 有 | `0x591740` | **2** |
| `m & 0xC1 == 0 && m & 0x22 != 0` | 北侧三格全无，E 或 W 有 | `0x591D80` | **2** |
| `m & 0x1C == 0 && m & 0x22 != 0` | 南侧三格全无，E 或 W 有 | `0x5910F0` | **2** |

**三个转角函数一个随机数都不取**（`0x593030` 被两条分支复用，只是喂进去的
矩形不同）。8 个取数点全在后四条分支里，每条两个，都是
`PickBelow(2)` 的**抖动**：把算好的坡道两端各随机挪 0 或 1 格。

```c
// 以 m & 0x70 == 0 那条为例（0x590BAE / 0x590BDD 两个取数点）
short x = cell->X - 1;
CellStruct p0 = { x, cell->Y - 4 };
CellStruct p1 = { x, cell->Y + 4 };
if (m & 0x80) { p0.Y = cell->Y - 3; p1.Y = cell->Y + 5; }   // 北边有 → 整体南移一格
if (m & 0x08) { p0.Y -= 1;          p1.Y -= 1;          }   // 南边有 → 再北移一格
p0.X += PickBelow(2);                                       // 抖动 1
p1.X += PickBelow(2);                                       // 抖动 2
made = FUN_00592440(&p0, &p1, targetRegionId);
```

⚠️ 两次抖动**都无条件抽**，与抽出来的值是否被采用无关。

⚠️ **八条分支不是互斥的**，只是"建成了就跳到收尾"。
一旦某条尝试失败，后面的分支还会继续评估、继续抽随机数。
穷举 256 个掩码可知后四条里**最多同时成立两条**
（例如 `m = 0x80` 同时满足第 5、6 条，`m = 0x22` 同时满足第 7、8 条），
所以一次 `BuildRamp` 的消耗上界是
「2 条直坡分支 × (2 次抖动 + 建造函数内部 1 次)」+「后路 4 个建造函数各 1 次」= **10**，
下界是 0（掩码全空、或中心盒子就没过）。
**这个"不是常数"的性质必须照抄**——`0x5905D0` 那 100 次重试里每一次的开销都不同。

### `tol` 的第二处作用：撑过 51 次就允许"硬开长坡"

```asm
00590E8B  fld   dword [esp+0x2C]   ; tol
00590E8F  fcomp dword [0x7E5168]   ; 0.5f
00590E97  test  ah, 0x41 / jne     ; tol <= 0.5 就跳过整段
00590EA0  test  cl, cl / jne       ; 已经建成了也跳过
```

`tol > 0.5f` 且前面八条全败，才追加四次**跨度固定 ±4 格、两端不抖动**的直坡尝试：

| 顺序 | 门 | 建造函数 | 两端 |
|---|---|---|---|
| 1 | `m & 0x08 == 0`（S 无） | `0x5910F0` | `{X−4, Y+1}` `{X+4, Y+1}` |
| 2 | `m & 0x02 == 0`（E 无） | `0x591740` | `{X+1, Y+4}` `{X+1, Y−4}` |
| 3 | `m & 0x08 == 0`（S 无） | `0x591D80` | `{X+4, Y−1}` `{X−4, Y−1}` |
| 4 | `m & 0x20 == 0`（W 无） | `0x592440` | `{X−1, Y−4}` `{X−1, Y+4}` |

⚠️ 第 1 条与第 3 条**用了同一个门 `m & 0x08`**，而这两个建造函数在主路里
对应的是"南侧全空"（`0x5910F0`）与"北侧全空"（`0x591D80`）两个相反方向。
按其余三条的规律，第 3 条**本该是 `m & 0x80`（N 无）**。
高度疑似复制粘贴漏改——按「bug 也要复刻」原样保留，列入 §十一。

这四次**自己不抖动**，但建造函数内部那 1 个取数点照走。

`tol = tries × 0.01f`，`0.01f` 的位形是 `0x3C23D70A = 0.009999999776482582`，
再加上 **RC=chop**：`tries = 50` 时 `50 × 0.01f` 截断成 `0.49999997f`，
**不大于** `0.5f`；所以这条后路是从 **`tries = 51`** 起才开的。
就近舍入会让它提前一次到 `tries = 50`——**这是舍入模式直接改变地图的一个实例**。

### 建造函数干了什么

四个直坡函数（`0x5910F0` / `0x591740` / `0x591D80` / `0x592440`）与三个转角函数
（`0x593030` / `0x593550` / `0x593AF0`）的骨架一致：

```c
if (!Clearance(&rect, targetRegionId)) return 0;     // 0x594010
for (每个落在 rect 里的格子 xy) {
    if (!InDiamond(&xy)) continue;                   // 0x5AC230
    SetWorkOwner(&xy, targetRegionId);               // 0x58D070
    Cell* c = TryGetCellAt(&xy);                     // 0x5657A0
    c->Level = FindRegionById(targetRegionId)->level; // 0x5943E0 的 +0x10
}
StampTile(...);                                      // 0x5A6C10，铺坡道砖
```

三个新查实的小零件：

| 地址 | 语义 |
|---|---|
| `0x594010` | 净空检查：矩形四角都要 `IsWithinUsableArea`，且**每格**的 `work.regionId` 只能是本区域（`this+0x08`）或目标区域，且 `IsPlain`（`0x486380`）。任一条不满足立刻返回 0 |
| `0x5943E0` | `FindRegionById`——线性扫全局区域向量比 `+0x08`，找不到返回 `NULL`（**调用方不检查**） |
| `0x5AC230` | `InDiamond(CellStruct*)`，就是 `0xABED04`/`0xABED08` 那四个不等式的独立封装 |
| `0x58D070` | `work[xy].regionId = id`（`work` 为空时静默不做） |

四个直坡函数各自还有 **1 个**取数点（`5914A4` `591AB2` `592186` `592838`），
都是 `PickBelow(2)`，用来在两张 5 项的悬崖砖表里选行：

```
[0x82AF3C] = { 3, 2, 1, 0, 0 }              ; 子格/朝向序列
[0x82AF50] = { 11, 15, 15, 15,  7 }         ; 行 0
[0x82AF64] = { 12, 16, 16, 16,  8 }         ; 行 1（= 行 0 各 +1）
```

两行的差别正好是 §八那几个"同形三连组"里的**相邻成员**
（`11/12` 属 `{11,12,13}`、`15/16` 属 `{14,15,16}`）——
也就是说这个 `PickBelow(2)` 挑的是坡道边上那道悬崖的**外观变体**。
**选行的下标其实是坡道走向的符号（`run > 0`），不是随机数**；
随机数控制的是另一处（走向翻转 / 起点偏移），**尚未逐条读完**。

## 五之三、`0x58F2C0` = BuildBridge：在水上铺一座低桥——5 个取数点全部落地

签名：`char BuildBridge(RegionClass* water /*ecx*/, Region* a, Region* b)`，
两个区域参数都在栈上（`[esp+0x20C]` / `[esp+0x210]`），进门就取
`idA = a->+0x08`、`idB = b->+0x08`。取证：`memory/data/decomp/rmg_bridge_stage4{g,h,i}.txt`。

三条独立证据把"架桥"从假说钉成结论：

1. 桥面写的 overlay 号 `0x4A..0x4D` / `0x53..0x56` / `0x5C` / `0x5E` / `0x60` / `0x62`，
   按 `[OverlayTypes]` 的位置下标翻出来正好是 `LOBRDG01..25`（低桥）；
2. 桥头两端各贴一块 `PavedRoadEnds` 的成员，而这个 TileSet **恰好只有 4 块砖**，
   代码用的正好是 `+0/+1/+2/+3` 对应东/北/西/南；
3. 桥上会 `new` 一间建筑，类型字符串是 `"CABHUT"`（桥梁维修小屋），
   业主字符串是 `"Neutral"`。

### 外壳：最多试 200 次

```c
char BuildBridge(RegionClass* water, Region* a, Region* b) {
    int  idA = a->id, idB = b->id;
    int  tries = 0;
    char done  = 0;
    for (;;) {
        if (done) return done;                  // 成功后从循环头出去
        ... 见下 ...
        if (199 < ++tries) return done;         // 判据在循环尾
    }
}
```

`done` 只在**铺完一整座桥**之后置 1，所以一次调用最多架一座桥；
失败的尝试之间**不回滚**（探条只读不写，桥面只在全部判据通过后才落笔）。

### 第一步：抽一格水面当锚点——三重拒绝采样

```c
// 取数点 0x58F342
do {
  do {
    do { idx = _ftol((double)(uint32)Random() * (1/(2^32-1)) * (double)(side*side)); }
    while (idx > side*side - 1);                      // 标准 PickBelow(side²)
  } while (work[idx].regionId != water->id);          // ① 必须是本水域自己的格
  CellStruct zero = CellStruct(0, 0);                 // 0x42D470
} while (work[idx].coord == zero);                    // ② 坐标不能是 (0,0)，0x50E470
int x = work[idx].coord.X, y = work[idx].coord.Y;
```

`side = [0x89C2DC]`，工作数组基址 `[0xABED10]`、步长 `0x50`、`regionId` 在 `+0x38`——
与前面各阶段同一套。这是外层循环里**唯一**的一个随机数点，
但两层拒绝都会重抽，所以"水域格子少"的地图会在这里多吃随机数。

> ① 里的 `water->id` 是**每轮重新从 `ecx` 里读**的（伪代码的 `iVar9 = local_1f4`），
> 说明作者不信任区域表在架桥过程中保持不变。

### 第二步：四条探条从锚点往外走，找对岸

四个方向各有一份**独立的栈上矩形**（编译器没合并，四份都在）：

| 方向 | 初始矩形 | 每步 | 侧翼两格 |
|---|---|---|---|
| 北 | `{x-1, y, 3, 1}` | `Y -= 1` | `(X, Y)` 与 `(X+2, Y)`，即 `x-1` / `x+1` |
| 南 | `{x-1, y, 3, 1}` | `Y += 1` | 同上 |
| 西 | `{x, y-1, 1, 3}` | `X -= 1` | `(X, Y)` 与 `(X, Y+2)`，即 `y-1` / `y+1` |
| 东 | `{x, y-1, 1, 3}` | `X += 1` | 同上 |

每个方向的走法完全一样（以北为例）：

```c
while (!IsCrossableGround(&strip, 0, 0)) {   // 0x5A7250，两个开关都传 0
    strip.Y -= 1;                            // 先走一步，再检查新行/新列的侧翼
    CellStruct l = { strip.X,     strip.Y };
    CellStruct r = { strip.X + 2, strip.Y };
    Cell* cl = TryGetCellAt(&l);             // 0x5657A0，ecx = 0x87F7E8
    Cell* cr = TryGetCellAt(&r);
    if (!IsWithinUsableArea(&l, 1) || !IsWithinUsableArea(&r, 1)   // 0x578460
        || IsCliffOrBridgeTile(cl) || IsCliffOrBridgeTile(cr))     // 0x4863D0
        goto fail;                           // 该方向对失败
}
Rect back = { strip.X, strip.Y - 3, 3, 3 };  // 落桥点再往外的 3x3 也得是好地
if (!IsCrossableGround(&back, 0, 0)) goto fail;
```

四个 `back` 矩形分别是：北 `{X, Y-3, 3, 3}`、南 `{X, Y+1, 3, 3}`、
西 `{X-3, Y, 3, 3}`、东 `{X+1, Y, 3, 3}`（`X`/`Y` 是走完之后的探条原点）。

两个要点：

- **循环判据在走之前**。如果锚点那条 `3×1` 一开始就已经是"可通行地面"，
  循环体一次都不执行，探条原点保持不动，跨距会算成 0；
- **成对短路**：北失败就**根本不走南**（共用 `fail` 标签 `0x58F649`），
  西失败也**根本不走东**（`0x58F84F`）。所以"竖桥可用"要求北南**都**成功。

### 第三步：配对判定——两端必须正好是 A 和 B

```c
int spanV = okV ? abs(Ysouth - Ynorth) : 999;      // abs 是 >>31 异或减
int spanH = okH ? abs(Xeast  - Xwest ) : 999;
// 落桥点的区域号：竖桥读探条自己的列 x-1，横桥读探条自己的行 y-1
int nId = work[Ynorth * side + (x-1)].regionId, sId = work[Ysouth * side + (x-1)].regionId;
int wId = work[(y-1) * side + Xwest ].regionId, eId = work[(y-1) * side + Xeast ].regionId;

if (okV && !((nId == idA && sId == idB) || (nId == idB && sId == idA))) okV = false;
if (okH) {
    if ((eId == idA && wId == idB) || (eId == idB && wId == idA)) {
        if (okV) { if (spanV < spanH) okH = false; else okV = false; }   // 平手偏横
    } else okH = false;
}
```

两端**必须**分别落在 A、B 上（顺序任意）；一端落到第三个区域就作废。
两个方向都可用时选**短的那个**，`spanV == spanH` 时走 `else` 分支砍掉竖桥——
**平手偏横**。

> 竖桥读列 `x-1`、横桥读行 `y-1`，用的是探条矩形的**左上角**而不是锚点本身。
> 探条宽 3、锚点在中间，所以这等于"看侧翼里靠西/靠北那一格"，不是中心格。
> 是有意还是顺手写了 `rect.X`，**未确认**；照抄。

### 第四步：跨距上限随重试次数放宽

```c
int maxSpan = tries / 25 + 8;    // 有符号除 25：imul 0x51EB851F + sar edx,3 + 符号修正
```

试到第 0..24 次上限 8，第 25..49 次上限 9……第 175..199 次上限 15。
**先横后竖**地挑：

```c
if (okH && spanH < maxSpan)      rect = { Xwest, y-1, spanH + 1, 3 };
else if (okV && spanV < maxSpan) rect = { x-1, Ynorth, 3, spanV + 1 };
if (rect.W > 0 && rect.H > 0) {
    if (!BridgeFootprintClear(&rect)) goto next_try;      // 0x5902C0
    ... 铺桥 ...
    done = 1;
}
```

`rect` 那四个变量在**每轮开头清 0**，所以两个 `if` 都不成立时
`W == H == 0`，直接落到重试。

### 第五步：铺桥面（不取随机数）

横桥：

```c
for (int yy = Y; yy < Y + H; ++yy)               // H 恒为 3
  for (int xx = X; xx < X + W; ++xx) {
      Cell* c = TryGetCellAt(&{xx, yy});
      if      (xx == X)         c->Overlay = 0x5E;             // LOBRDG21
      else if (xx == X + W - 1) c->Overlay = 0x5C;             // LOBRDG19
      else                      c->Overlay = 0x4A + (xx % 4);  // LOBRDG01..04
      TryGetCellAt(&{xx, yy})->OverlayData = (char)(yy - Y);   // 0..2，第二次查格
  }
```

竖桥把内外循环换个身份、砖号换一组：

```c
for (int yy = Y; yy < Y + H; ++yy)
  for (int xx = X; xx < X + W; ++xx) {          // W 恒为 3
      if      (yy == Y)         c->Overlay = 0x60;             // LOBRDG23
      else if (yy == Y + H - 1) c->Overlay = 0x62;             // LOBRDG25
      else                      c->Overlay = 0x53 + (yy % 4);  // LOBRDG10..13
      c->OverlayData = (char)(xx - X);                         // 0..2
  }
```

overlay 号 → 名字（`nn = overlay - 0x49`）：

| overlay | 名字 | 用处 |
|---|---|---|
| `0x4A`..`0x4D` | `LOBRDG01`..`04` | 横桥桥身，按 `x % 4` 循环 |
| `0x53`..`0x56` | `LOBRDG10`..`13` | 竖桥桥身，按 `y % 4` 循环 |
| `0x5C` | `LOBRDG19` | 横桥东端 |
| `0x5E` | `LOBRDG21` | 横桥西端 |
| `0x60` | `LOBRDG23` | 竖桥北端 |
| `0x62` | `LOBRDG25` | 竖桥南端 |

号→名的换算走 `[OverlayTypes]` 的**位置下标**（键 1..39 → 下标 `key-1`，
键 42..121 → 下标 `key-3`），用已经钉死在 exe 上的两个锚点
（`GEM01 = 27`、`TIB01 = 102`，见 [stage-tiberium.md](stage-tiberium.md)）双向校验过。

`Overlay` 是 `CellClass +0x44`（4 字节写），`OverlayData` 是 `+0x11E`（1 字节）。
这一条顺手把 [stage-lats.md](stage-lats.md) 里 "`+0x11E` 疑似 `OverlayData`"
的猜测坐实了：桥身用它存**桥面第几行/第几列**（0..2），正是 overlay 子帧号的语义。

> `xx % 4` 是 **C 的有符号取余**（`and 0x80000003` + 负数修正），不是 `& 3`。
> 坐标为负时余数会是负的、算出比 `0x4A` 更小的 overlay 号。
> 实战中 `X >= 1`（锚点来自可用区），所以走不到；**照抄不修**。
>
> 每格查了**两次** `TryGetCellAt`（一次写 overlay、一次写 OverlayData）。
> 纯粹是源码写了两条语句、编译器没做 CSE。

### 第六步：桥头路砖——4 个 `PickBelow(2)`

四端各有一个"先看地形平不平、平的话再抛硬币"的结构。
抛硬币用 `[0x7ED8B0] = 2/(2^32-1)`、拒绝 `> 1`，即 `PickBelow(2)`：

| 端 | 试探矩形（`0x5A7440`） | 取数点 | 抛到 1 | 抛到 0 或矩形不平 |
|---|---|---|---|---|
| 东 | `{X+W, Y-2, 6, 6}` | `0x58FBDD` | `PavedRoads+10` @ `(X+W, Y)` | `PavedRoadEnds+0` @ `(X+W, Y)` |
| 西 | `{X-6, Y-2, 6, 6}` | `0x58FCC6` | `PavedRoads+9` @ `(X-4, Y)` | `PavedRoadEnds+2` @ `(X-1, Y)` |
| 北 | `{X-2, Y-6, 7, 6}` | `0x59000E` | `PavedRoads+13` @ `(X, Y-4)` | `PavedRoadEnds+1` @ `(X, Y-1)` |
| 南 | `{X-2, Y+H, 7, 6}` | `0x5900F7` | `PavedRoads+12` @ `(X, Y+H)` | `PavedRoadEnds+3` @ `(X, Y+H)` |

- 横桥只走东/西两行，竖桥只走北/南两行——**一次成功的架桥只消耗 2 个随机数**，不是 4 个；
- 矩形不平时**照样贴砖**，只是贴 `PavedRoadEnds`（一小截断头路）而不是整段公路；
  这两条分支里 `PickBelow(2)` 是**不抽**的（`goto` 跳过了取数点）；
- 贴砖用压砖器 `StampTile(ecx = tileIdx, edx = &coord, -1, -1)` = `0x5A6C10`，
  与 §八 坡道那边同一个入口，`level = -1` ⇒ 不改层高；
- `PavedRoadEnds` 只有 4 块砖、这里正好 `+0/+1/+2/+3` 四方向各用一块——
  这是 `0xABBEC4 = PavedRoadEnds` 这个命名的**第二条独立佐证**
  （第一条是 `0x545150` 的键名回填，见 [stage-recalc.md](stage-recalc.md)）。

### 第七步：桥梁维修小屋——整条流水线里唯一一次 `new` 游戏对象

`0x5904B0(Rect*)` 在一个 **(W+1)×(H+1) 闭区间**里找第一格
「没有 overlay、是平地、`+0xE4 == 0`」的格子，找到就在那儿盖屋：

```c
char PlaceRepairHut(Rect* r) {
    for (int yy = r->Y; yy <= r->Y + r->H; ++yy)
      for (int xx = r->X; xx <= r->X + r->W; ++xx) {
        Cell* c = TryGetCellAt(&{xx, yy});
        if (c->Overlay != -1 || !IsPlain(c) || c->+0xE4 != 0) continue;   // 0x486380
        int ht = HouseTypeIndexByName("Neutral");        // 0x5117D0，"<random>" 会返回 -2
        HouseClass* h = FindHouseByTypeIndex(ht);        // 0x502D30，比 house->+0x34 的 +0xB8
        int bt = FindBuildingTypeIndexById("CABHUT");    // 0x45E7B0，strcmpi 0x7C8D20 比 +0x24
        BuildingClass* b = new(0x720) BuildingClass(BuildingTypes[bt], h);  // ctor 0x43B740
        b->Unlimbo({ xx * 256 + 128, yy * 256 + 128, 0 }, 0);              // vfunc +0xD8
        return 1;
      }
    return 0;
}
```

两端各试**两个**矩形（小的不行就试大的）：

| 桥向 | 第一端 | 第一端退化 | 第二端 | 第二端退化 |
|---|---|---|---|---|
| 横 | `{X, Y-1, 2, 5}` | `{X-1, Y-2, 3, 7}` | `{X+W-2, Y-1, 2, 5}` | `{X+W-2, Y-2, 3, 7}` |
| 竖 | `{X-1, Y, 5, 2}` | `{X-2, Y-1, 7, 3}` | `{X-1, Y+H-2, 5, 2}` | `{X-2, Y+H-2, 7, 3}` |

对复刻的影响很实在：**这是目前逆出来的整条 RMG 里唯一一处构造游戏对象的地方**，
落到 `.map` 上不是砖数据，而是 `[Structures]` 里的一条 `CABHUT` 记录
（业主 `Neutral`、坐标就是那格）。

> `new` 返回 0 时代码**不检查**就往下走（`0x59057B xor eax,eax` → `0x5905A3 mov edx,[eax]`
> 取虚表）。25 年前 `operator new` 失败就直接崩，这是那个年代的常规写法；
> 记录在案，C++ 侧不会真的分配失败，行为上无差别。

### 这次新落地的公共零件

| 地址 | 语义 |
|---|---|
| `0x5A7250(Rect*, allowRoad /*dl*/, allowRoadEnd /*栈*/)` | 「这块矩形是可跨的地面」：先内联查四角在菱形内（`[0xABED04]` / `[0xABED08]` 四不等式），再逐格：是 `PavedRoads` 砖要 `allowRoad`、是 `PavedRoadEnds` 砖要 `allowRoadEnd`、否则必须 `IsPlain(0x486380) 或 MiscPaveTile(0x486650) 或 PaveTile(0x486670)`。**`0x58F2C0` 的全部调用点两个开关都传 0** |
| `0x5A7440(Rect*, allowRoad)` | 同上，**再加**「每格的 `+0x11B`（Level）都等于左上角那格」——桥头路砖要求地面同高 |
| `0x5902C0(Rect*)` | 桥的落脚区检查：**(W+1)×(H+1) 闭区间**，每格要 `Overlay(+0x44) == -1`、`Level` 等于锚格、且 `IsPlain 或 IsWaterOrShore(0x4865D0)` |
| `0x4866D0` | `TileIndex ∈ [PavedRoads(0xABBEC8), +0x0F)` |
| `0x4866F0` | `TileIndex ∈ [PavedRoadEnds(0xABBEC4), +4)` |
| `0x486650` / `0x486670` | `∈ [MiscPaveTile(0xAA10A4), +0x0E)` / `∈ [PaveTile(0xABC2B0), +0x10)` |

`0x486650` / `0x486670` 这两个谓词在 [stage-lats.md](stage-lats.md) 里
是「阶段 10 的死分支、语义未确认」；现在**语义确认**了（铺装地面的两个砖号区间），
只是在阶段 4 这边仍然走不到 `true`——它们挂在 `||` 链的右侧，会被求值但没有铺装砖。

> `0x4866D0` 的硬编码宽度 `0x0F`（15）与温和气候 `PavedRoads` 实际
> `TilesInSet = 21` 不一致——这是 `0x4863D0` 里 `CliffRamps` 那个
> `0x14` vs `10` 之外的**第二例**"硬编码宽度与 ini 不符"。
> 三例已汇总在 `memory/data/rmg/tileset_globals.txt` 的交叉校验尾注里。照抄不修。

### 未确认（本节）

1. 落桥点区域号读的是探条矩形左上角（竖桥列 `x-1`、横桥行 `y-1`）而不是中心格——
   是刻意还是笔误，**未确认**；
2. `CellClass +0xE4`（小屋选址的第三个条件）字段名**未确认**（多阶段共用的未确认项）；
3. 横桥分支里那句多余的 `H < 1` 复查（伪代码 `local_1f8 < 1`）只在竖桥路径上有意义，
   是编译器合并两条路径公共尾部的产物，**不影响行为**。

## 六、`0x578E60(0, -1)`：全图重刷砖

不是 RMG 专用的函数（`0x87F8xx` 那几个是 DisplayClass 的视野矩形）。它做四件事：

1. 工作数组为空就 `new (side² × 0x50)` 并逐个构造（`0x58BDC0`），
   **并记住"是我分配的"**，收尾时再释放——所以它可以脱离 RMG 单独被调用；
2. 把每格工作记录的 `+0x4A` 置 1（`0x58C2C0` 按下标取记录）；
3. 两遍全图迭代：
   - 第一遍 `FUN_00579010(cell, -1)`；
   - 第二遍只对 `cell->+0x11C == 0`（不是坡）且 `IsPlain(cell)`（`0x486380`）的格子
     调 `FUN_00579620(cell, -1)`；
   两遍都以返回 `false` 为**提前终止**条件；
4. 清掉 `[0x880990]` 与 `[0x88098C]`（贴砖器用的那对暂存全局，
   见 [stage-seeding-water.md](stage-seeding-water.md) §七）。

两遍之间那四句 `_DAT_0087F8F4/F8/FC/900 = …` 是把"可视矩形"重置成整张图，
让迭代器扫全图而不是扫屏幕。`0x579010` / `0x579620` 本身不取随机数。

## 七、`0x5A19E0`：悬崖直角处随机削一格 / 垫一格

全图迭代，对每一格取五个邻格与一张**高差掩码**（§九的 `0x579B70`）：

```c
Cell* n5 = cell->Neighbour(5 /*SW*/);
Cell* n1 = cell->Neighbour(1 /*NE*/);
Cell* n3 = cell->Neighbour(3 /*SE*/);
Cell* n2 = cell->Neighbour(2 /*E*/);
Cell* n6 = cell->Neighbour(6 /*W*/);
uint m = HigherMask(cell);
```

三条互斥分支，掩码常数就是三个 90° 的直角：

| 掩码 | 位含义 | 条件 | 动作 |
|------|--------|------|------|
| `0x83` | N + NE + E | `HigherMask(n3) == 0x83` 且 `IsPlain(n2)` 且 `PickBelow(2) == 0` | `n2->Level -= 4`；`work[n2].regionId = work[cell].regionId` |
| `0x38` | S + SW + W | `HigherMask(n3) == 0x38` 且 `IsPlain(n6)` 且 `PickBelow(2) == 0` | `n6->Level -= 4`；`work[n6].regionId = work[cell].regionId` |
| `0xE0` | W + NW + N | `HigherMask(n5) == 0xE0` 且 `HigherMask(n1) == 0xE0` 且 `IsPlain(cell)` | `cell->Level += 4`；`work[cell].regionId = work[n6].regionId` |

四个直角里**第四个 `0x0E`（E + SE + S）没有分支**。是漏写还是刻意，未确认——
按「bug 也要复刻」照缺。

另外三处必须照抄的细节：

1. **`0xE0` 那条不抛硬币**，另外两条各抽一次 `PickBelow(2)`（取数点
   `5A1A7B` / `5A1B2C`），只有抽到 0 才动手。三条分支的随机数开销不对称；
2. 层高的加减写在 `byte` 上（`add cl, 0xFC` = `-4`），**会回绕**，不做钳位；
3. 工作数组为空（`[0xABED10] == 0`）时**层高照改，只是不写 `regionId`**——
   `test ecx, ecx / je` 那条跳过的只有最后一句写入。

## 八、`0x5A17F0` + `0x5A1350`：多格悬崖砖重掷同形变体

### `0x5A1350`：在 CliffSet 的三连组里换一个**别的**成员

`[0xAA1020]` 这个 TileSet 全局，`tileset_globals.txt` 当初留了三个候选键名
（`CliffSet` / `ToSnowTheater` / `ToTemperateTheater`）。现在能定死了：

* 温和与雪地两套 theater ini 里 `ToSnowTheater` / `ToTemperateTheater`
  **根本没有定义**；
* `CliffSet` 在两套 theater 里都是 `TileSet0010`、起始砖号 **49**、
  `TilesInSet = 40 = 0x28`、`FileName = Cliff`、`RequiredForRMG = true`；
* 而 `0x5A17F0` 的入口判据正好是 `IsoTileTypeIndex ∈ [base, base + 0x28)`。

⇒ **`[0xAA1020] = CliffSet`**。

函数体是一个 21 路 switch（`cmp ecx,0x20 / ja` + 字节索引表 `0x5A17CC`（33 项）
+ 地址表 `0x5A1778`（21 项），MSVC6 标准分派）：

```c
int RerollCliff(int tile) {
    int rel = tile - CliffSet;      // 0..39
    switch (rel) { ... }
    return CliffSet + rel';
}
```

21 个 case（20 个实处理 + 1 个 default）的分组结果，`rel` 是相对 CliffSet 起点的 0 基下标：

| 三连组 | `rel` | 换成 |
|--------|-------|------|
| `{4,5,6}` `{8,9,10}` `{11,12,13}` `{14,15,16}` `{22,23,24}` `{34,35,36}` | 组内第 1 个 | 第 2 或第 3 个 |
| 同上 | 组内第 2 个 | 第 1 或第 3 个 |
| 同上 | 组内第 3 个 | 第 1 或第 2 个 |
| 对换 | `28` ↔ `29` | 彼此（不取随机数） |
| 其余 `rel`（含 `rel < 4`、`rel > 36`） | — | 原样返回 |

"换成另外两个之一"这件事，前五组是**内联展开**的，第六组（`rel 34..36`）
走**外联的** `PickRange`（`0x598030`）。三种写法值得单独记：

```asm
; 组内第 1 个（rel 4 那组，5A137B）：靠一句 fadd 1.0 把下界顶上去
5A137D  call Randomizer::Random           ; ecx = 0xABE890
5A138F  fild qword [esp+4]                ; 补成 64 位有符号，无符号→浮点的标准绕法
5A1393  fmul qword [0x7ED8B0]             ; × 2/(2³²−1)   → [0.0, 2.0]
5A1399  fadd qword [0x7E1718]             ; + 1.0         → [1.0, 3.0]
5A139F  call _ftol                        ; 向零截断       → {1, 2, 3}
5A13A4  cmp eax, 2 / ja 5A137D            ; 3 重抽        → {1, 2}
5A13AF  add eax, 4                        ; → rel 5 / rel 6
; 组内第 2 个：没有 +1.0，抽 {0,1} 之后 ×2 跳过自己
  … cmp eax, 1 / ja 重抽 / lea eax,[eax+eax+4]   ; → rel 4 / rel 6
; 组内第 3 个：抽 {0,1} 直接用
  … cmp eax, 1 / ja 重抽 / add eax, 4            ; → rel 4 / rel 5
```

第六组（`rel 0x22..0x24`）走外联版，`__fastcall ecx=lo, edx=hi`，三条一目了然：

```asm
5A172C  rel 0x22:  ecx=1, edx=2 -> PickRange(1,2)          + 0x22   ; -> 0x23 / 0x24
5A1738  rel 0x23:  ecx=0, edx=1 -> lea eax,[eax+eax+0x22]           ; -> 0x22 / 0x24
5A1757  rel 0x24:  ecx=0, edx=1 -> PickRange(0,1)          + 0x22   ; -> 0x22 / 0x23
5A1704  rel 0x1C:  直接 mov eax,0x1D                                ; 常数对换，无取数
5A1718  rel 0x1D:  直接 mov eax,0x1C
5A1766  default :  eax 原样（就是传进来的 rel）
```

`[0x7ED8B0] = 4.656612874161595e-10 = 2 / (2³²−1)`——是 `span = 2` 的比例常数
预乘好的版本，和 `0x598030` 里 `× span × 1/(2³²−1)` 两步乘**不是同一条浮点路径**，
不要互相替换。

**结论：这个函数永远不会返回原来那块砖**（三连组内换一个、`28↔29` 对换）。
Ghidra 把 `fadd 1.0` 折进了返回值，直接读伪代码会误以为第一个成员能抽中自己。

### `0x5A17F0`：找出"确实是一整块多格悬崖砖"再重掷

```c
for (Cell* c : MapCells) {
    if (c->IsoTile < CliffSet || c->IsoTile >= CliffSet + 0x28) continue;
    Cell* s = c->Neighbour(4 /*S*/);
    Cell* e = c->Neighbour(2 /*E*/);
    for (Cell* nb : { s, e }) {
        if (nb->IsoTile != c->IsoTile) continue;
        if (!(nb->SubTile < c->SubTile)) continue;      // 无符号比较
        int alt = RerollCliff(c->IsoTile);              // 0x5A1350
        if (alt == c->IsoTile) continue;                // 只有"原样返回"那一路会相等
        int w = TileTypes[c->IsoTile]->Width;           // +0x2E4
        CellStruct origin = { nb->X - nb->SubTile % w,
                              nb->Y - nb->SubTile / w };
        int blob = work ? work[origin].regionId : -1;   // +0x38
        StampTile(alt, &origin, blob, -1);              // 0x5A6C10，level = -1
    }
}
```

「同砖号 + 邻格子块号更小」这一条是在**判定这两格真属于同一块被压下去的多格砖**，
而不是两块碰巧一样的 1×1 砖。原点由**邻格**（子块号更小的那个）反推。

压砖用的是压砖器 `0x5A6C10` 而非贴砖器 `0x57B440`，`level = -1`
⇒ **不改层高、但会重写 `+0x11C` 坡度**（两者的差别见
[stage-seeding-water.md](stage-seeding-water.md) §六）。

## 九、公共零件

### `0x579B70` = 八邻域「比我高 4 层」掩码

```c
uint HigherMask(CellClass* cell) {
    int L = cell->Level;                        // +0x11B，按 signed char 读
    if (!InDiamond(cell->xy)) return 0;         // 0x87F8DC / 0x87F8E0 四不等式
    if (work[cell->xy].+0x4A == 0) return 0;    // 0x58C2A0 取工作记录
    uint m = 0;
    for (每个方向) {
        Cell* n = 该方向的邻格;                  // 出图取 MapClass::InvalidCell
        if (!CoordinatesLegal(n->xy)) continue;
        if (n->Level != L + 4) continue;
        if (IsCliffOrBridgeTile(n)) continue;   // 0x4863D0
        m |= 该方向的位;
    }
    return m;
}
```

位定义与水掩码 `0x57B210` **完全一致**（[stage-seeding-water.md](stage-seeding-water.md)
§七）：

> `bit k ⟺ AdjacentCell[(k + 1) & 7]`
> ⇒ bit0=NE、bit1=E、bit2=SE、bit3=S、bit4=SW、bit5=W、bit6=NW、bit7=N。

只是判据从"是水砖"换成了"层高恰好比我高 4，且不是悬崖/桥类砖"。
`+ 4` 是硬编码的一整级台阶——与 `0x5A19E0` 的 `±4`、压砖器的 `-4` 同一套单位。

### `0x4863D0` = 「这格是悬崖/坡道/桥/瀑布类砖」

按 TileSet 起始砖号逐个区间判：

| TileSet 全局 | 键名 | 硬编码宽度 |
|---|---|---|
| `0xAA1020` | `CliffSet` | `0x28` |
| `0xAA073C` / `0xABB110` / `0xAA1050` / `0xAA10A0` | 四个 `Waterfall*` | `4`（**带子块号例外**，见下） |
| `0xABBEBC` | `CliffRamps` | `0x14` |
| `0xABAD24` | `WaterCaves` | `4` |
| `0xAA0E28` | `BridgeSet` | `0x10` |
| `0xABAD1C` | `WoodBridgeSet` | `0x10` |
| `0xABC2C8` | `DestroyableCliffs` | `2` |
| `0xAA101C` | `WaterCliffs` | `0x1C` |

（全局值为 `-1` 表示该 theater 没定义这一组，整条判据跳过。）

四个瀑布组有个额外的**子块号豁免**：只有落在组内首块或末块、
且 `SubTile` 属于特定的两个值时才**不算**：

| 组 | 豁免的 `SubTile` |
|---|---|
| `WaterfallEast` | 0 或 4 |
| `WaterfallWest` | 1 或 3 |
| `WaterfallSouth` | 0 或 1 |
| `WaterfallNorth` | 2 或 3 |

也就是"瀑布正对水流的那两个子格不算障碍"。四组的豁免子块号各不相同，
和 [stage-water-maptype34.md](stage-water-maptype34.md) §六里
`[+0]/[+3]/[+2]/[+1]` 的贴砖顺序是同一套朝向约定。

> **未确认**：`CliffRamps` 的硬编码宽度 `0x14 = 20`，但温和/雪地两套 theater 里
> `CliffRamps` 指向的 `TileSet0025` 只有 **10** 块砖。多出来的 10 会盖到下一组去。
> 是有意覆盖两组、还是照着别的 theater 写死的，暂无判据。

### `0x58C6F0` = 距离 + 角度 + 噪声的三项优先级

`0x58D620`（区域切分）用它给候选格排序，是
[stage-water-maptype34.md](stage-water-maptype34.md) §七那个 `AngularGrow`
优先级的**超集**：

```c
float Priority(CellStruct* origin, CellStruct* c, double baseAngle) {
    int dx = c->X - origin->X, dy = c->Y - origin->Y;
    double d = FastSqrt((double)(dy*dy + dx*dx));         // 0x4CAC40
    double a;
    if (dx == 0) a = π/2;                                 // [0x7E2820]，double
    else { a = Atan(-(double)dy / (double)dx);            // 0x4CADE0，查表
           if (dx < 0) a += π; }                          // [0x7E44D0]
    a = fabs(a - baseAngle);
    while (a >= 2π) a -= 2π;                              // [0x7E3CC0]
    if (a > π) a = 2π - a;
    return 0.15 * d                                       // [0x7ED6B0]
         + 1.5  * a                                       // [0x7ED6B8]
         + (double)(uint32)Random() * (1/(2³²−1)) * 2.5;  // [0x7ED898] × [0x7ED6A8]
}
```

三个权重是 `0.15 / 1.5 / 2.5`——**噪声项的权重最大**，所以切出来的边界很毛。
`fabs` 与取模的先后顺序和 `AngularGrow` 里一样别扭（先 `fabs` 再模 `2π`），
照抄。

⚠️ 这个函数**每算一次优先级就吃一个随机数**，而它是被逐个候选格调用的——
和瀑布那边一样，`0x58D620` 的随机数开销是**按候选格数计**的，不是常数。

## 十、随机数账本（45 处）

地址取自 `memory/data/rmg/rng_ledger.txt` 的阶段 4 一节（指令级可达性扫描的结果）。

| 函数 | 取数点数 | 位置 | 状态 |
|---|---|---|---|
| `0x58C6F0` 三项优先级 | 1 | `58C7AE` | ✅ 已实锤（§九） |
| `0x58C800` `SeedRegion` | 1 | `58CAF9` | ✅（阶段 3 已实锤，此处共用） |
| `0x58D620` 区域切分 | 4 | `58D782` `58D7CD` `58D8BD` `58E37D` | ⚠️ 仅结构 |
| `0x58F2C0` 架桥 | 5 | `58F342` `58FBDD` `58FCC6` `59000E` `5900F7` | ✅ 已实锤（§五之三；一次成功架桥至少消耗 3 个，锚点拒绝采样会更多） |
| `0x5905D0` 开通路 | 3 | `590792` `5907C0` `590889` | ✅ 三处语义已实锤（§五） |
| `0x590970` 开一条坡道 | 8 | `590BAE` `590BDD` `590C7B` `590CAB` `590D4A` `590D79` `590E17` `590E47` | ✅ 已实锤（§五之二，四对坡道端点抖动） |
| `0x5910F0` 南向直坡 | 1 | `5914A4` | ⚠️ 已知是 `PickBelow(2)`，选什么未定 |
| `0x591740` 东向直坡 | 1 | `591AB2` | ⚠️ 同上 |
| `0x591D80` 北向直坡 | 1 | `592186` | ⚠️ 同上 |
| `0x592440` 西向直坡 | 1 | `592838` | ⚠️ 同上 |
| `0x598000` `UniformUnit` | 1 | `598003` | ✅（多阶段共用） |
| `0x598030` `PickRange` | 1 | `59805E` | ✅（多阶段共用） |
| `0x5A1350` 悬崖变体 | 15 | `5A137D` … `5A16CC`（连续 15 个 case） | ✅ 已实锤（§八） |
| `0x5A19E0` 直角削垫 | 2 | `5A1A7B` `5A1B2C` | ✅ 已实锤（§七） |

合计 45，与 [rng-ledger.md](rng-ledger.md) 的阶段归属逐条对上。

**已关掉 37 处，剩 8 处**：`0x58D620`（4），
外加四个直坡建造函数各 1 处（已知都是 `PickBelow(2)`，只差"选的是什么"）。

⚠️ 「取数点数」是**静态**的指令条数，不是运行时的消耗次数。
`0x5A1350` 的 15 个点里一次调用只走 1 个（switch 分派），
而 `0x58C6F0` 的那 1 个点会被**每个候选格**各走一遍。
换算成实际消耗要等算法全部还原（[rng-ledger.md](rng-ledger.md) §六未确认 1）。

## 十一、未确认与 TODO

1. `0x58D620`（区域切分）只还原到前半段：分配 `(cellCount·2+10)` 个节点 + 五整数堆、
   把成员格的 `regionId` 标成 `-2`、`PickRange(n/8, n/3)` 定切走多少格、
   `PickBelow(cells.count)` 抽种子、`Random() × [0x7ED8A8] (= 2π/(2³²−1))` 定基准角，
   然后进 `0x58C6F0` 优先级驱动的堆扩散。**后半段与两个尾部取数点未逐条读**；
2. `0x590970` 的**外壳已实锤**（§五之二，8 个取数点全部落地，
   第三个参数的两处作用都查清了）；仍欠的是七个建造函数
   （`0x5910F0` / `0x591740` / `0x591D80` / `0x592440` / `0x593030` /
   `0x593550` / `0x593AF0`）的**砖号计算与落点几何**——
   已知骨架是「净空检查 → 逐格改 owner 与 Level → `StampTile`」，
   四个直坡函数各自那 1 个 `PickBelow(2)` 具体决定什么还没读完
   （表 `0x82AF50` / `0x82AF64` 的选行下标是走向符号，不是随机数）；
2a. **疑似复制粘贴 bug**：`0x590970` 后路第 3 次尝试（`0x591D80`，主路对应"北侧全空"）
   的门写的是 `m & 0x08`（S），与第 1 次（`0x5910F0`，"南侧全空"）撞车，
   按规律应为 `m & 0x80`（N）。未找到反证，照抄；
3. ~~`0x58F2C0` 架桥只是假说~~ —— **已实锤**，见 §五之三（`LOBRDG` 低桥 +
   `PavedRoadEnds` 桥头 + `CABHUT` 维修小屋）。本节遗留的三条小疑点搬到 §五之三末尾；
4. `0x5910F0` / `0x591740` / `0x591D80` / `0x592440` —— 各 1 个取数点，
   挂在 `0x5905D0` 子树下，连调用关系都还没画清；
5. `0x579010` / `0x579620`（`0x578E60` 里的两遍重刷砖）—— 不取随机数，
   但会**改砖号**，落 C++ 时必须还原；
6. `RegionClass +0x1B` 的读取方**未找到**——只看到 `0x5905D0` 在开通路失败时清 0；
7. `0x5A19E0` 缺 `0x0E`（E+SE+S）那个直角分支，是漏写还是刻意，未确认；
8. `0x4863D0` 里 `CliffRamps` 的硬编码宽度 `0x14` 比实际 `TilesInSet = 10` 大一倍
   （同类第二例：`0x4866D0` 的 `PavedRoads` 宽 `0x0F` vs 实际 21，见 §五之三）；
9. `0x58D410 GetBorderCells` 会不会返回空表——决定 §五那处 `count − 1` 下溢
   是不是真能被触发。

