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
double mn = <立即数 0x416312CF_E0000000>;
for (auto d : avgDist) if (d < mn) mn = d;

bool flag = (seed->Resources == 3 &&
             (seed->MapType == 1 || seed->MapType == 3 || seed->MapType == 4));

for (int w = 0; w < r->startCount; ++w)
    CreateOreField(GetWaypointCoords(wpBase + w), _ftol(...) + 500,
                   wpBase + w + 1, flag);
wpBase += r->startCount;
```

`mn` 算出来了却**没看到被用在后面的 `_ftol(...)` 里**——那一段的浮点表达式
Ghidra 丢了（`WARNING (jumptable): Unable to track spacebase`）。
**未确认**，需要单独反汇编 `0x5A2790`–`0x5A2860`。

## 三、`CreateOreField`（`0x5A28C0`）：三个取数点

调用约定有点怪：4 个参数走栈（`cellptr, size, waypointNum, isSpecial`），
但调用前还 `mov ecx, [esp+0x28]`（= `MapSeedClass*`）——
Ghidra 判成 `__cdecl` 4 参，**`ecx` 是否被用未确认**。

函数体是一个**最小堆扩散**，形状与陆地涂块 `FUN_0059BBC0` 一模一样：
`new` 一个 `size * 0x50` 的记录数组 + 一个容量 `size*10` 的堆。

| 取数点 | 惯用法 | 常量 | 语义 |
|--------|--------|------|------|
| `0x5A2B8E` | `_ftol(r * 3/(2^32-1) + 1.0)`，拒绝 `> 3` | `0x7ED8C0 = 3/(2^32-1)`、`0x7E1718 = 1.0` | 取值 **{1,2,3}**；门禁 `[ebp+0x14] == 0` |
| `0x5A2C18` | `_ftol(r * 12/(2^32-1))`，拒绝 `> 11` | `0x7EDA90 = 12/(2^32-1)` | `field->+0x44` 从 `-1` 变成 `base + [0,11]`；**12 档，疑似矿的生长阶段，未确认** |
| `0x5A2D79` | `FastSqrt(d2) + r * 5.0 * (1/(2^32-1))` | `0x7ED7C0 = 5.0`、`0x7ED898 = 1/(2^32-1)` | **无拒绝采样**；最小堆的优先级 = 距离 + `[0,5]` 抖动，存成 `float` |

第三个就是陆地涂块用的那个「距离 + 均匀抖动」优先级——
说明矿田和陆地块共用同一套「有机形状」生成手法。

扩散过程中往工作记录写 `work[Y*side+X].+0x3C = [ebp+0x10]`
（`0x5A2DD1`），即给格子打上矿田编号。

## 四、本阶段随机数账

| 取数点 | 位置 | 何时消耗 |
|--------|------|---------|
| `0x5A2494` | `0x5A23A0` | 仅当区域**没有**出生点（`startCount < 1`）且 `doPick` |
| `0x598003` | `NormalDeviate` -> `UniformUnit` | 每个候选点抽一次抖动，**缓存命中时不花** |
| `0x5A2B8E` | `0x5A28C0` | 每片矿田，门禁 `[ebp+0x14] == 0` |
| `0x5A2C18` | `0x5A28C0` | 每格首次赋值时 |
| `0x5A2D79` | `0x5A28C0` | 最小堆每次入队 |

## 五、未确认

1. ~~`MapSeedClass +0x2BC` / `+0x2C0` 来源未确认~~ → **已实锤**，见 §六
2. §2.5 里 `mn`（各出生点到候选点的平均距离的最小值）到底怎么进入
   `_ftol(...) + 500` 那个表达式——Ghidra 丢了栈跟踪，需要单独反汇编
3. `0x5A28C0` 的最小堆扩散细节：接受/拒绝谓词、`size` 如何充当预算、
   `[ebp+0x10]` / `[ebp+0x14]` 是什么对象的字段
4. `field->+0x44` 的 12 档到底是不是矿的生长阶段（`OverlayData`）
5. `0x5A28C0` 调用时 `ecx = MapSeedClass*` 是否真被使用
6. ✅ 已关：`0x71BB90` = **`TerrainClass::TerrainClass(TerrainTypeClass*, CellStruct*)`**，
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
