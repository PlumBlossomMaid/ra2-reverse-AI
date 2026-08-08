# 阶段 8：Creating tiberium——布矿

> 状态：**外层 `0x5A23A0` 已完整还原**（选参开关、参考点、最近候选点、矿量公式、
> 正态抖动的接受窗口）；内层矿田生长 `0x5A28C0` **只还原到骨架**，
> 三个取数点的惯用法与常量已实锤，生长细节待补（见 §五）。
> 取证：`memory/data/decomp/rmg_tiberium.txt`。

## 一、要不要挑「特殊矿田」：一张真值表

```c
switch (seed->MapType) {              // +0x3C
  case 0:                 doPick = (seed->Resources == 3); break;   // +0x40
  case 1: case 3: case 4: doPick = (seed->Resources != 3); break;
  case 2:                 doPick = true;                    break;
  default:                doPick = false;                   break;
}
```

`MapType` 与 `Resources` 交叉决定；默认 `MapType=1`、`Resources=1` → **`doPick = true`**。

## 二、逐区域处理

```c
int wpBase = 0;
for (int ri = 0; ri < regionCount; ++ri) {        // [0xABDFA0] / [0xABDF94]
    Region* r = regions[ri];
    if (r->candidates == NULL) continue;          // +0x00
    int special = -1;
    ...
}
```

`RegionClass +0x00` 这次也定了：它是一个 **`DynamicVectorClass<CellStruct>*`**
（数据 `+0x04`、个数 `+0x10`，元素 4 字节 = packed XY），
装的是这个区域的**候选点**，由阶段 6 的 `0x594F40` 填。

### 2.1 参考点：出生点重心，或者随机摸一格

```c
short refX, refY;
if (r->startCount >= 1) {                      // +0x20
    int sx = 0, sy = 0;
    for (int i = 0; i < r->startCount; ++i) {
        CellStruct wp = ScenarioClass::GetWaypointCoords(i + wpBase);
        sx += wp.X; sy += wp.Y;
    }
    refX = (short)((short)sx / r->startCount);  // 0x5A246F 的 cdq/idiv，向零截断
    refY = (short)((short)sy / r->startCount);
} else {
    // 取数点 0x5A2494：PickBelow(r->cells.Count)
    int n = r->cells.Count;                     // +0x38
    int k; do { k = PickBelow_inline(n); } while (k > n - 1);
    CellStruct c = r->cells[k];                 // +0x2C
    refX = c.X; refY = c.Y;
}
```

注意累加用的是 **16 位** `bx`/`bp`（`0x5A245E` 的 `add bx, ax`），
除法前才 `movsx` 扩成 32 位——**出生点多、坐标大时会溢出回绕**。照抄。

### 2.2 离参考点最近的那个候选点 = `special`

```c
int best = 500000;                              // 0x7A120
for (int i = 0; i < r->candidates->Count; ++i) {
    CellStruct c = r->candidates->data[i];
    int d2 = (c.Y - refY)*(c.Y - refY) + (c.X - refX)*(c.X - refX);
    double d = FastSqrt((double)d2);            // 0x4CAC40，14 位查表近似
    if (d < (double)best) { best = _ftol(d); special = i; }
}
```

比较是 `fild (int)best` 后 `fcompp` + `test ah,1`（C0 = 小于），**严格小于**。
注意 `best` 每次都被截断成整数再参与下一轮比较——不是保留 double。

### 2.3 总矿量

```
005A2558  fild dword [seed + 0x54]      ; Tiberium
005A2565  fmul qword [0x7E3808]         ; x 0.01
005A256B  fild dword [esp+0x38]         ; (hi - lo)
005A256F  fmulp st(1)
005A2571  fiadd dword [esp+0x20]        ; + lo
005A2575  call 0x7C5F00                 ; _ftol
```

```c
int lo = seed->RMGMinimumTiberium;   // +0x2BC，rules.ini [General]，默认 2500
int hi = seed->RMGMaximumTiberium;   // +0x2C0，rules.ini [General]，默认 5500
int v  = (int)((double)seed->Tiberium * 0.01 * (double)(hi - lo) + lo);

double mult = max((double)r->startCount, 0.5);  // fcomp 0.5 + test ah,0x41
int total = (int)((double)v * mult);

if (r->candidates->Count == 0 || total == 0) continue;
int avg = total / r->candidates->Count;         // 整数除法
```

即「矿量参数当百分比，在 `[lo,hi]` 之间插值」，再按这个区域的出生点数放大——
没有出生点的区域按 **0.5 倍**算（不是 0）。

### 2.4 每个候选点开一片矿田：正态抖动

```
005A25C9  mov  ecx, 0xABDFB8
005A25CE  call 0x5980C0                 ; NormalDeviate（内部 2 次 UniformUnit = 取数点 0x598003）
005A25D3  fmul qword [0x7E4F50]         ; x 50.0
005A25D9  fst  qword [esp+0x20]
005A25DD  fcomp qword [0x7EDA88]        ; 与 -100.0 比
005A25E5  test ah, 1                    ; C0：g < -100 -> 重抽
005A25E8  jne  0x5A25C9
005A25EA  fld  qword [esp+0x20]
005A25EE  fcomp qword [0x7E2AC0]        ; 与 +100.0 比
005A25F6  test ah, 0x41                 ; C0|C3：g < 100 或 g == 100 才通过
005A25F9  je   0x5A25C9
005A25FB  fild dword [esp+0x38]         ; avg
005A25FF  fadd qword [esp+0x20]         ; + g
005A2603  call 0x7C5F00                 ; _ftol
```

```c
for (int i = 0; i < r->candidates->Count; ++i) {
    double g;
    do { g = NormalDeviate() * 50.0; } while (g < -100.0 || g > 100.0);
    int size = (int)((double)avg + g);
    if (size >= 0)
        CreateOreField(r->candidates->data + i, size,
                       i + wpBase + 1, (i == special));   // 0x5A28C0
}
```

三个要点：

1. **接受窗口是闭区间 `[-100, +100]`**（sigma=50 的正负 2 sigma）：
   `accept` 当且仅当 `g >= -100 && g <= 100`
2. `NormalDeviate` 的 `this` 是全局 `0xABDFB8`，**缓存跨阶段串**：
   这里第一次抽可能直接命中上一阶段留下的缓存值，一个随机数都不花。
   拒绝重抽时同理——缓存里那个也要用掉才会再调 `UniformUnit`
3. `size < 0` 时**跳过这个候选点，但随机数已经花了**

### 2.5 再给每个出生点开一片

```c
Vector<double> avgDist(0, 0);  avgDist.growth = 10;    // 0x5ADAB0 构造，vtable 0x7EDA6C
for (int w = 0; w < r->startCount; ++w) {
    CellStruct wp = ScenarioClass::GetWaypointCoords(w + wpBase);
    double s = 0.0;
    for (int i = 0; i < r->candidates->Count; ++i)
        s += FastSqrt(dist2(wp, r->candidates->data[i]));
    avgDist.push_back(s / r->candidates->Count);
}
double mn = 9999999.0;                                 // 0x416312CF_E0000000
for (auto d : avgDist) if (d < mn) mn = d;

bool isGems = (seed->Resources == 3 &&
               (seed->MapType == 1 || seed->MapType == 3 || seed->MapType == 4));

for (int w = 0; w < r->startCount; ++w) {
    int size = _ftol((avgDist[w] - mn) * 15.0) + 500;   // 0x5A27F4-0x5A280A
    CellStruct wp;
    ScenarioClass::GetWaypointCoords(&wp, wpBase + w);  // 0x68BCC0
    CreateOreField(&wp, size, wpBase + w + 1, isGems);  // 0x5A28C0
}
wpBase += r->startCount;
```

**`mn` 的用法已实锤**（`0x5A27F4-0x5A280A`）：

```
005a27f4  fld  qword ptr [edx + esi*8]     ; avgDist[w]
005a27f7  fsub qword ptr [esp + 0x58]      ; - mn
005a27fb  fmul qword ptr [0x7ed7b0]        ; x 15.0
005a2801  call 0x7c5f00                    ; _ftol
005a280a  add  eax, 0x1f4                  ; + 500
```

即 **`size = (int)((avgDist[w] - mn) * 15.0) + 500`**。

语义很直白：`mn` 是"离候选点群最近的那个出生点"的平均距离，
所以**离矿区最近的那个出生点拿到的矿田恰好是 500 格**，
其他出生点每远 1 格就多 15 格矿——**越偏远补偿越多**。
这也解释了为什么原版地图边角玩家家门口的矿常常大得离谱。

`_ftol` 是向零截断，且 `avgDist[w] - mn >= 0` 恒成立，所以等价于 `floor`；
但**复刻仍要写成截断**，负零/边界上两者才不会分家。

## 三、`CreateOreField`（`0x5A28C0`）：三个取数点——**已完整还原**

签名（栈传参 4 个）：

```c
void FUN_005A28C0(CellStruct* seed, int count, int fieldId, char isGems);
```

调用前还 `mov ecx, [esp+0x28]`（= `MapSeedClass*`），但函数体一次都没读 `ecx`
（`0x5A28C0` 开头直接 `sub esp` 建帧、参数全从 `[ebp+8..0x14]` 取）——
**是编译器留下的无用赋值**，复刻时忽略。

`isGems` 只用来选 overlay 基址（`0x5A2953-0x5A2967`）：

```
005a2953  neg  cl
005a295c  sbb  ecx, ecx                  ; isGems ? -1 : 0
005a2962  and  ecx, 0xffffffb5
005a2967  add  ecx, 0x66                 ; isGems ? 0x1B(27) : 0x66(102)
```

即**矿基址 102、宝石基址 27**——正好对上 RA2 的 `Riparius`(102..113) /
`Cruentus`(27..38) 两组各 12 帧 overlay。这也**关掉了「12 档是什么」的旧疑问**：
`+0x44` 不是"生长阶段"，是**overlay 序号本身**，12 是那组 overlay 的帧数。

### 3.1 骨架

与 `0x5A45E0`（树林）/ `0x5A4B60`（LAT）/ `0x59BBC0`（陆地涂块）同一个最小堆模板，
但多了一层「**播不下去就换地方重来**」的外壳：

```c
pool = new Node[count*10];        // new(count*0x50)，Node = { int32 xy; float key; }
heap = new MinHeap(count*10);     // new(0x14) + new(count*0x28+4)
placed = 0; restarts = 0; used = 0; cur = NULL; anchored = false;
origin = *seed;
while (placed < count) {
    if (restarts >= 10) break;                       // 最多重来 10 次
    if (cur == NULL) {                               // 冷启动 / 扩散断了
        heap.clear();
        for (all cells) Work[cell].+0x3C = 0;        // 全图清田块 id
        used = 1;  pool[0] = { *seed, 0.0f };
        Work[*seed].+0x3C = fieldId;
        heap.push(&pool[0]);  cur = heap.pop();
        restarts++;  origin = *seed;  anchored = false;
    }
    cell = TryGetCellAt(cur);
    if (Work[*cur].+0x45 == 0) {                     // 未锁定
        if (!anchored) {                             // 本次爆发的第一格
            origin = *cur;  anchored = true;
            heap.clear();  used = 0;                 // ← 池子从头覆写
            if (!isGems) {
                do { i = _ftol(r * 3/(2^32-1) + 1.0); } while ((uint)i > 3);   // 0x5A2B8E
                sprintf(buf, "TIBTRE0%d", i % 10);   // 0x82C090 = "TIBTRE0%d"
                t = new(0xE0);
                if (t) TerrainClass::TerrainClass(t, TerrainTypes[FindIndex(buf)], &origin);
            }
        }
        if (cell->+0x44 == -1) {
            do { v = _ftol(r * 12/(2^32-1)); } while ((uint)v > 11);            // 0x5A2C18
            cell->+0x44 = v + base;                  // 102 矿 / 27 宝石
        } else if (cell->+0x11E <= 10) {
            cell->+0x11E++;                          // 已有矿 -> 加厚一层
        } else {
            goto neighbours;                         // 满了：不计入 placed
        }
        placed++;
    }
neighbours:
    for (d = 0; d < 8; d++) {
        n = *cur + Adj[d];                           // 邻接表 [0x89F688]
        if (!MapClass::IsWithinUsableArea(&n, 1)) continue;
        nc = TryGetCellAt(&n);
        if (!IsPlain(nc)) continue;
        bool ok = (nc->+0x44 == -1 && Work[n].+0x3C != fieldId)
               || (nc->+0x11E < 11 && nc->GetContainedTiberiumIndex() != -1);
        if (!ok) continue;
        pool[used].xy  = n;
        pool[used].key = (float)( FastSqrt((double)((ox-nx)² + (oy-ny)²))
                                  + (double)(uint32)Random() * 5.0 * (1/(2^32-1)) );  // 0x5A2D79
        Work[n].+0x3C = fieldId;
        heap.push(&pool[used]);  used++;
    }
    cur = heap.pop();
}
```

### 3.2 三个取数点

| 取数点 | 惯用法 | 常量 | 语义 |
|--------|--------|------|------|
| `0x5A2B8E` | `_ftol(r * 3/(2^32-1) + 1.0)`，拒绝 `> 3` | `[0x7ED8C0] = 3/(2^32-1)`、`[0x7E1718] = 1.0` | `PickRange(1,3)` → `TIBTRE01`..`TIBTRE03`；**`TIBTRE00` 永远不出现**。只在 `isGems == 0` 时抽 |
| `0x5A2C18` | `_ftol(r * 12/(2^32-1))`，拒绝 `> 11` | `[0x7EDA90] = 12/(2^32-1)` | `PickBelow(12)`，`+0x44 = base + [0,11]`，即 overlay 序号 |
| `0x5A2D79` | `fild r; fmul 5.0; fmul 1/(2^32-1); fadd sqrt; fstp dword` | `[0x7ED7C0] = 5.0`、`[0x7ED898]` | 最小堆优先级 = 到**本次爆发原点**的近似距离 + `[0,5)` 抖动，存成 `float` |

### 3.3 三个容易踩的点

1. **距离的原点是 `origin`，不是 `seed`**。`origin` 在"本次爆发第一格未锁定"时
   被改成那一格；扩散断掉重来时又被拨回 `seed`。
2. **`anchored` 翻转时把池子和堆全清了**（`used = 0`），
   已经入队的邻居**连带作废**——这是"以第一个可用格为真正中心重新长"的写法。
3. **`+0x44` 已被占且 `+0x11E > 10` 的格子既不加厚也不计入 `placed`**，
   但仍然照常展开 8 邻居。所以一片吃满的老矿田会把预算白白拖着转。

### ⚠️ 原版怪癖：`used = 0` 之后 `cur` 指着的那一条会被当场覆写

第 2 条那个「池子从头覆写」不是清空——**内存一个字节都没动**，动的只有写指针。
写点在 `0x5A2C73`：

```
005a2c6d  mov  edi, dword ptr [ebp-0x6c]        ; edi = pool（每轮循环重取）
005a2c70  mov  eax, dword ptr [ebp-0x64]        ; eax = used
005a2c73  lea  edi, [edi + eax*8]               ; 写指针 = pool + used
```

而 `cur`（`[ebp-0x74]`）还指着老槽位。冷启动之后 `cur` 恒是 **0 号槽**，
`anchored` 一翻转 `used` 就归零，于是**同一轮的第一个被接受的邻居就写进 0 号槽**，
把 `cur` 指着的坐标改掉了。邻域循环每个方向都重新 `movsx` 读一遍 `*cur`
（`0x5A2C7F` 起），所以后面几个方向算的已经是**新坐标**的邻居——
这一条必然触发（不是边角情况），复刻时池子必须建模成「按下标寻址、
回绕之后旧数据还在」的内存，不能用「清空 vector」代替。

同理，最小堆里存的是**槽位下标**而不是 key 的副本，比较时得当场回池子取值。

### ⚠️ 池子没有上界，且堆满之前一定先越界

`0x5A2916` 算出来的 `count*10 - 1` 存进 `[ebp-0x40]` 之后**一次都没被读**，
入池那段（`0x5A2D40`）也没有任何 `cmp`。堆的上浮倒是有门槛
（`count + 1 >= cap` 就静默丢弃），可两边的容量都是 `count*10`，
而 `used` 是**每接受一个邻居就 +1**、与 push 成不成功无关——
所以「堆满」这条路必然在「池子越界」之后才走得到。池子越界是真·未定义行为
（紧邻的多半就是堆数组自己），**没法逐字节复刻**。
对拍脚本因此钩住 `0x5A2D40` 盯着 `used`，任何用例只要摸到 `count*10` 就直接报错，
基准里不收这种样本。

第二条接受条件 `(nc->+0x11E < 11 && GetContainedTiberiumIndex() != -1)`
**没有 `fieldId` 保护**，同一格可以被反复入队——池子就是这么被撑爆的。

### ⚠️ 原版怪癖：全图清 `+0x3C` 在循环里

每次"冷启动"都要**把全图 `Work[].+0x3C` 清 0 再跑一遍迭代器**（`0x5A29E0` 一带）。
最坏情况一片矿田要清 10 次全图。功能上没错（`fieldId` 从 1 起算），但是 O(10·N)。
原样复刻即可，不要改成增量清理——**清的时机会影响 `Work[].+0x3C != fieldId` 那个判定**。

## 四、本阶段随机数账

| 取数点 | 位置 | 何时消耗 |
|--------|------|---------|
| `0x5A2494` | `0x5A23A0` | 仅当区域**没有**出生点（`startCount < 1`）且 `doPick` |
| `0x598003` | `NormalDeviate` -> `UniformUnit` | 每个候选点抽一次抖动，**缓存命中时不花** |
| `0x5A2B8E` | `0x5A28C0` | 每次“爆发锚定”一次，且 `isGems == 0` |
| `0x5A2C18` | `0x5A28C0` | 每格首次赋值时 |
| `0x5A2D79` | `0x5A28C0` | 最小堆每次入队 |

## 五、未确认

1. ~~`MapSeedClass +0x2BC` / `+0x2C0` 来源未确认~~ → **已实锤**，见 §六
2. ✅ 已关：`size = _ftol((avgDist[w] - mn) * 15.0) + 500`（§2.5）。
   `ScenarioClass::GetWaypointCoords` = `0x68BCC0`（`*out = this->Waypoints[idx]`，字段 `+0x632`）
3. ✅ 已关：`0x5A28C0` 的扩散细节已完整还原（见 §三）。
   `[ebp+0x10]` = `fieldId`、`[ebp+0x14]` = `isGems`、`size` = **要铺满的格数**。
4. ✅ 已关：`+0x44` 的 12 档不是生长阶段，是 **overlay 序号**——
   `base + PickBelow(12)`，矿 `base=102`（Riparius 102..113）、
   宝石 `base=27`（Cruentus 27..38）。真正的“厚度”是 `+0x11E`（0..11）。
5. ✅ 已关：`0x5A28C0` 体内**一次都没读 `ecx`**，那句 `mov ecx` 是无用赋值。
6. ✅ 已关：`MapClass::IsWithinUsableArea(&xy, 1)` 的第二个参数是 **`use_height`**。
   `0x578460` 开头 `test al, al` 为真时才走 `0x57848E` 那段内联的 `GetCellAt`，
   读 `+0x11B`（`movsx`，有符号层高）与 `+0x11C`（坡面）把上下边界各抬 `level` 格；
   `+0x11C != 0` 时下边界再多让一格（`0x5784B4`）。为假时 `adj` 恒为 0。
   ⚠️ 那段内联取格子拿不到实体格就退到共享哑元 `0xABDC50` 并把坐标写进它的 `+0x24`
   ——菱形外的邻格读到的层高/坡面是**上一次的残留**。
7. `CellClass::GetContainedTiberiumIndex` 的返回值口径——**未确认**（只用了 `!= -1`）。
8. ✅ 已关：`0x71BB90` = **`TerrainClass::TerrainClass(TerrainTypeClass*, CellStruct*)`**，
   就是「在这格新建一个地貌物体」的构造函数。取证在阶段 10：
   `0x5A4887-0x5A48AA` 处 `new(0xE0)` 之后紧接着调它，类型取自
   `TerrainTypes[FindIndex("TREE%d%d")]`（`[0xA8E31C]` 是 `TerrainTypeClass` 数组）。
   Ghidra 把它标成 `TerrainClass::WhatAmI` 是误标，不要信。
   → 详见 [stage-lats.md](stage-lats.md) §四

## 六、`+0x2BC` / `+0x2C0` 实锤：rules.ini `[General]`

```
005982bb  mov  eax, dword ptr [esi + 0x2bc]     ; 默认值（构造函数里填的 2500）
005982c1  mov  ecx, dword ptr [0x82b134]        ; -> 0x826278 "General"
005982c7  push eax
005982c8  push 0x82bdb8                         ; "RMGMinimumTiberium"
005982cd  push ecx
005982d0  call 0x5276d0                         ; CCINIClass::ReadInteger
005982db  mov  dword ptr [esi + 0x2bc], eax
005982d5  mov  edx, dword ptr [esi + 0x2c0]     ; 默认值 5500
005982e7  push 0x82bda4                         ; "RMGMaximumTiberium"
005982ef  call 0x5276d0
005982f7  mov  dword ptr [esi + 0x2c0], eax
```

即：

| 偏移 | rules.ini `[General]` 键 | 默认值 |
|------|-------------------------|--------|
| `+0x2BC` | `RMGMinimumTiberium` | 2500 |
| `+0x2C0` | `RMGMaximumTiberium` | 5500 |

所以 §2.3 的总矿量就是
`v = (int)(Tiberium% x 0.01 x (RMGMaximumTiberium - RMGMinimumTiberium) + RMGMinimumTiberium)`——
按 CLAUDE.md 的「数据驱动」要求，这两个值必须**从 rules.ini 注入**，不许写死。

## 七、复刻状态：✅ §三 已落 C++ 并逐位对拍；§二 已落但暂无对拍

`code/rewrite/rmg/ore_field.{h,cpp}`：

| 本文的东西 | 原版地址 | C++ | 对拍 |
|---|---|---|---|
| §三 `CreateOreField` 全体 | `0x5A28C0` | `CreateOreField` | ✅ ctest `ore_field` |
| §三 的池子 + 一基最小堆 | `0x5A2AD0` / `0x5AD870` | `OreNode` / `OreHeap` | ✅ 同上 |
| §一 真值表、§二 全部 | `0x5A23A0` | `PlaceTiberium` | ⚠️ 尚无基准 |

**§三 的对拍强度：黄金对拍，逐位一致。** 基准由
`code/randmap/dump_tiberium_golden.py` 用 Unicorn 跑 `0x5A28C0` 真身取得，
存档 `memory/data/rmg/tiberium_golden.txt`。它自足到只需要换掉四个外部依赖：

| 桩 | 为什么可以换 |
|---|---|
| `0x485010 GetContainedTiberiumIndex` | 真身走 rules.ini 的 `TiberiumType` 表（`0x5FDD20` 遍历 `[0xB0F4EC]`），与本函数逻辑无关。桩按「格子上是矿 102..113 或宝石 27..38 就返回 0」给，C++ 侧的 `OreWorld::ContainedTiberiumIndex` 用同一条规则 |
| `0x7C8EF4 sprintf` | 自己格式化 `"TIBTRE0%d"` 并记下名字 |
| `0x71DD80 FindIndex` | 恒返回 0，只记名字 |
| `0x71BB90 TerrainClass::TerrainClass` | 空操作，只记 (名字, 落点) |

7 个用例，比的是**随机数个数**、每个变了的格子的 `(+0x44, +0x11E, +0x3C)`、
建出来的 TIBTRE 棵数/名字/落点，以及池子用掉的槽位数：

| 用例 | 要压的东西 |
|---|---|
| `plain` | 全未定砖的常规一片（含上面那条「`used = 0` 之后 0 号槽被覆写」） |
| `gems` | `isGems`：基址 27，且**不抽** `0x5A2B8E`、不建 TIBTRE |
| `anchor` | 一条 `+0x45` 斜带——不落矿、不触发爆发锚定，但照样展开 8 邻居 |
| `preore` | 预置矿格：走「加厚 `+0x11E`」，以及那条没有 `fieldId` 保护的接受条件 |
| `island` | 只留两格能扩散：撞满 `restarts >= 10`（10 次全图清 `+0x3C`、10 棵 TIBTRE，而 `count = 40` 一格也铺不满） |
| `edge` | 种子在菱形东端，大量邻格被 `0x578460` 挡掉 |
| `blocked` | 五分之一是已定砖 |

§二 那部分（`PlaceTiberium`）目前只有静态取证，**还没有基准**：它要
`RegionSet`、出生点表、`ScenarioClass` 的路标表一起就位，等阶段 6 的
端到端基准搭好之后再补。
