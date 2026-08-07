# 阶段 9：Creating hills——造山

> 入口 `0x5A35F0`（`__thiscall`，`ecx = MapSeedClass*`）。
> RNG 总账里本阶段只有 1 处取数点 `0x598003`，即 `NormalDeviate` 内部的
> `UniformUnit`——**全部随机性集中在 `0x5A2F50` 这一个高度场生成器里**，
> 每个可用格子抽 2 次正态。
> 取证：`memory/data/decomp/rmg_hills.txt`、`rmg_hills2.txt`、`rmg_hills3.txt`、
> `rmg_height.txt`（角点抬升子系统六个函数）。

```
005a35f9  call 0x5a33f0     ; ① 打「不许动」的锚点
005a35fe  call 0x5a2f50     ; ② 生成连续高度场（唯一取随机数的地方）
005a3605  call 0x6b2a70     ; ③ 重建角点高度格网（无随机）
          <主循环>          ; ④ 把高度场落到地形上（无随机，§四~§七）
005a36c1  call 0x6b3850     ; ⑤ 依角点格网刷砖与坡面（无随机，§八）
          <2x2 收尾遍>      ; ⑥ 压平不合法的 2x2 坡块（无随机，§九）
```

③~⑤ 那套角点格网属于引擎的"抬升地形"通用子系统（`0x6B2400-0x6B4300` 一带，
RMG 与地图编辑器共用），本文 §三、§五~§八 已把 RMG 会走到的路径全部拆开。

## 一、`0x5A33F0`：标锚点

遍历全图格子（`0x578350` 复位迭代器 + `0x578290` 取下一个）：

```c
for (CellClass* c : MapCells) {
    if (IsShorePiece(c)) {                       // 0x4865B0
        for (int d = 0; d < 8; ++d) {
            CellStruct n = c->xy + AdjacentCell[d & 7];   // 表在 0x89F688
            if (!InUsableDiamond(n)) continue;
            TryGetCellAt(&n);                    // 0x5657A0（返回值只用于谓词）
            if (IsPlain(n)) {                    // 0x486380：+0x38 == 0 或 0xFFFF
                work[n.Y*side + n.X].height = 0.5;   // +0x08，字节 0x3FE0000000000000
                work[n.Y*side + n.X].lock   = 1;     // +0x45
                break;
            }
        }
    } else if (IsCliffOrBridge(c)) {             // 0x4863D0
        for (int d = 0; d < 8; ++d) {
            /* 同上，但只置 lock，不给初始高度 */
            work[...].lock = 1;
            break;
        }
    }
}
```

`AdjacentCell[8] = (0,-1)(1,-1)(1,0)(1,1)(0,1)(-1,1)(-1,0)(-1,-1)`。
这里是**全 8 向**、找到第一个平地就 `break`。

`InUsableDiamond`（`0x5AC230`，RMG 自己的可用区判定，菱形）：

```c
X + Y >  [0xABED04] && X - Y < [0xABED04] &&
Y - X <  [0xABED04] && X + Y <= [0xABED08]
```

### 两个新实锤的谓词

| 地址 | 语义 | 判据 |
|------|------|------|
| `0x4865B0` | **是岸砖** | `+0x38 ∈ [ShorePieces, ShorePieces+42)`，`ShorePieces = [0xABAD28]` |
| `0x4863D0` | **是悬崖/桥/瀑布** | 见下表，任一命中即真 |

`0x4863D0` 覆盖的砖组（全部用 `code/randmap/dump_tileset_globals.py` 的键名表对上）：

| 全局 | 键名 | 张数 | 备注 |
|------|------|------|------|
| `0xAA1020` | `CliffSet` | 40 | |
| `0xAA073C` | `WaterfallEast` | 4 | 首/末张还要看 `+0x11A` 不属于 {0,4} |
| `0xABB110` | `WaterfallWest` | 4 | `+0x11A` 不属于 {1,3} |
| `0xAA1050` | `WaterfallSouth` | 4 | `+0x11A` 不属于 {0,1} |
| `0xAA10A0` | `WaterfallNorth` | 4 | `+0x11A` 不属于 {2,3} |
| `0xABBEBC` | `CliffRamps` | 20 | |
| `0xABAD24` | `WaterCaves` | 4 | |
| `0xAA0E28` | `BridgeSet` | 16 | |
| `0xABAD1C` | `WoodBridgeSet` | 16 | |
| `0xABC2C8` | `DestroyableCliffs` | 2 | |
| `0xAA101C` | `WaterCliffs` | 28 | |

`lock`（工作记录 `+0x45`）在后面 `0x6B2520` / `0x6B4100` / `0x6B2A70` 里都会被读——
**打了锚点的格子不允许被抬升**。

## 二、`0x5A2F50`：高度场（唯一的随机源）

### 2.1 三个由 `Ruggedness` 派生的尺度

```
005a2f59  fild dword [ecx + 0x44]        ; H = (double)seed->Ruggedness（有符号）
005a2f6a  fmul qword [0x7edaa8]          ; x 0.0001
005a2f70  fadd qword [0x7e3860]          ; + 0.1      -> [esp+0x58]  drift
005a2f7c  fmul qword [0x7edaa0]          ; H x 0.0025 -> [esp+0x50]  baseSlope
005a2f8c  fmul qword [0x7e44e8]          ; H x 0.005  -> [esp+0x80]  maxSlope
005a2f99  fld  qword [esp+0x50]
005a2f9d  fcomp qword [0x7ed7c8]         ; 与 sigma0 = 0.025 比
005a2fa5  test ah, 1
005a2fa8  jne  0x5a33e8                  ; baseSlope < 0.025 -> 整个函数直接返回
```

```c
double drift     = Ruggedness * 0.0001 + 0.1;   // 高度漂移步长
double baseSlope = Ruggedness * 0.0025;         // 边界外的默认坡度
double maxSlope  = Ruggedness * 0.005;          // 坡度上限
const double sigma0 = 0.025;
if (baseSlope < 0.025) return;                  // 见下：实际门槛是 Ruggedness <= 10
```

**`Ruggedness <= 10` 时本阶段一个随机数都不取。**（`MapSeedClass +0x44 = Ruggedness`，
见 `docs/random-map/parameters.md`）

> ⚠️ **门槛是 `<= 10` 而不是 `< 10`，这是舍入模式咬出来的。**
> `0x7EDAA0` 存的 `0.0025` 是 `3F647AE147AE147B`，精确值略大于 1/400；
> `10 x 它` 的精确积落在 `0x3F99999999999999`（0.024999999999999998）与
> `0x3F9999999999999A`（0.025）**之间**。原版全程 RC=chop，于是向零截断成前者，
> `< 0.025` 成立，`Ruggedness == 10` 的图**一座山也没有**。
> 换成 round-to-nearest 就会得到正好 0.025，判定翻面。
> 模拟执行实测（`dump_height_field_golden.py`，FPCW=0x0E7F）：
> `Ruggedness` 9/10 → 全表 0 个非零格；11/12 → 55 个。
>
> 这条也是踩过坑才发现的：给模拟器忘了写 `FPCW = 0x0E7F` 时，
> `Ruggedness == 10` 的基准跑出了满地起伏，与重写对不上——**先怀疑模拟器**
> 这条经验又一次生效。

### 2.2 工作记录里的两个 double

| 偏移 | 含义 |
|------|------|
| `+0x08` | `height`，连续高度，最终四舍五入成整数 |
| `+0x10` | `slope`，这一格的「起伏强度」，充当下一次正态抽样的 sigma |
| `+0x45` | `lock`，阶段 ① 打的锚点 |

### 2.3 主循环：按线性下标 `Y*side + X` 顺序就地推进

因为下标递增，左邻 `(X-1, Y)` 与上邻 `(X, Y-1)` **都已经算好了**——
这是一个顺序推进的分形高度场，不是并行的。

```c
for (int i = 0; i < side*side; ++i) {
    Work* w = &work[i];
    if (w->xy == 0) continue;                  // packed XY 为 (0,0) 跳过
    short X = w->xy.X, Y = w->xy.Y;

    w->slope = 0.0;
    double diag = 0.0, grad = 0.0;
    if (InUsableDiamond(X-1, Y-1)) diag = work[(Y-1)*side + (X-1)].height;

    if (InUsableDiamond(X-1, Y)) {             // 左邻
        Work* L = &work[Y*side + (X-1)];
        w->height += L->height;
        w->slope   = L->slope;
        grad       = L->height - diag;
    } else {
        w->slope   = baseSlope;
    }

    if (InUsableDiamond(X, Y-1)) {             // 上邻
        Work* U = &work[(Y-1)*side + X];
        w->height += U->height;
        w->slope  += U->slope;
        grad      += U->height - diag;
    } else {
        w->slope  += baseSlope;
    }

    w->height *= 0.5;                          // 取左/上两邻的平均
    w->slope  *= 0.5;

    // 梯度只留符号，幅度换成固定的 drift
    grad = (grad > 0) ? +drift : (grad < 0) ? -drift : 0.0;

    if (w->lock) {                             // 锚点：高度不许为负，坡度按下限重置
        if (w->height <= 0.0) w->height = 0.0;
        w->slope = 0.0025;                     // 立即数 0x3F647AE147AE147B
    }

    // —— 第一次抽样：更新 slope，限制在 [0, maxSlope] ——
    double lo = -w->slope, hi = maxSlope - w->slope;
    double s = TruncNormal(0.0, sigma0, lo, hi);     // 取数点 0x5A3258
    w->slope += s;

    // —— 第二次抽样：更新 height，限制在 [-2, +2] ——
    lo = -2.0 - w->height; hi = 2.0 - w->height;
    double mu = clamp(grad, lo, hi);
    double h = TruncNormal(mu, w->slope, lo, hi);    // 取数点 0x5A3334，sigma 就是新 slope
    w->height += h;
}
```

`grad` 只留符号这一步的汇编（`0x5A316C`–`0x5A31A9`）：
`fld grad; fcomp 0.0; test ah,0x41; jne` 走「小于等于 0」分支；大于 0 取 `+drift`，
小于 0 取 `-drift`（`fld drift; fchs`），等于 0 保持 0。
**这就是山脊会沿着一个方向延伸的原因**——高度增量的均值一直朝同一边偏。

### 2.4 `TruncNormal(mu, sigma, lo, hi)` 的确切写法

两处抽样共用同一套骨架（`0x5A3215`–`0x5A3281` 与 `0x5A32ED`–`0x5A335D`）：

```c
double TruncNormal(double mu, double sigma, double lo, double hi) {
    // 兜底：若 [mu-sigma, mu+sigma] 与 [lo, hi] 完全不相交，就把分布重铺到区间上
    if (mu - sigma > hi || mu + sigma < lo) {
        sigma = (hi - lo) * 0.5;
        mu    = sigma + lo;
    }
    double v;
    do {
        v = NormalDeviate() * sigma + mu;      // this = 0xABDFB8
    } while (v < lo || v > hi);                // 闭区间接受
    return v;
}
```

第一次抽样进来时 `mu = 0`、`sigma = 0.025`，兜底条件退化成
`hi < -0.025 || lo > 0.025`——正常情况下（`slope` 落在 `[0, maxSlope]`）永远不触发。

拒绝判定的汇编：

```
005a3265  fst  qword [esp+0x70]          ; v
005a3269  fcomp qword [esp+0x40]         ; 与 lo 比
005a326f  test ah, 1                     ; C0: v < lo -> 重抽
005a3272  jne  0x5a3253
005a3274  fld  qword [esp+0x70]
005a3278  fcomp qword [esp+0x10]         ; 与 hi 比
005a327e  test ah, 0x41                  ; 非 C0 且非 C3: v > hi -> 重抽
005a3281  je   0x5a3253
```

> 两处抽样里 `[esp+0x10]` / `[esp+0x40]` 谁是 `lo` 谁是 `hi` **是反过来的**
> （第一次 `[esp+0x40]=lo`、`[esp+0x10]=hi`；第二次 `[esp+0x10]=lo`、`[esp+0x40]=hi`）。
> 读汇编时容易看串，已逐条核对。

由于 `NormalDeviate` 的缓存跨阶段、跨调用串（一次 Box-Muller 出两个值），
**一次 `TruncNormal` 花掉的 `Random()` 个数不是定值**。

### 2.5 收尾：高度取整是「四舍五入远离零」，不是 ties-to-even

```
005a3392  fld  qword [edi+eax+8]         ; h
005a3396  fcomp qword [0x7e2800]         ; 与 0.0 比
005a33a4  test ah, 1
005a33a7  je   0x5a33be                  ; h >= 0 -> 走下面
005a33a9  fsub qword [0x7e1738]          ; h - 0.5
005a33af  call 0x7c5f00                  ; _ftol（向零截断）
...
005a33be  fcomp qword [0x7e2800]
005a33c6  test ah, 0x41
005a33c9  jne  0x5a33e2                  ; h == 0 -> 原样不动
005a33cb  fld  qword [esi]
005a33cd  fadd qword [0x7e1738]          ; h + 0.5
005a33d3  call 0x7c5f00
```

```c
for (int i = 0; i < side*side; ++i) {
    double h = work[i].height;
    if      (h < 0.0) work[i].height = (double)(int)(h - 0.5);   // trunc
    else if (h > 0.0) work[i].height = (double)(int)(h + 0.5);
    // h == 0.0 原样
}
```

**注意这不是 `std::nearbyint`（ties-to-even）**，而是 round-half-away-from-zero。
复刻时这一处必须单独写，不能套项目里通用的 `nearbyint` 约定。

## 三、`0x6B2A70`：角点高度格网（无随机）

### 3.1 格网本身

先 `free` 旧的 `[0xB0B6EC]`，再 `operator new((H+1)*(W+1)*8)`，
矩形取自 `[0x87F90C..0x87F918]`，尺寸/原点缓存进四个全局：

| 全局 | 值 | 含义 |
|------|-----|------|
| `[0x8759A8]` | `[0x87F90C]` | `originX` = 地图矩形左上角 X |
| `[0x8759AC]` | `[0x87F910]` | `originY` |
| `[0x8759A4]` | `[0x87F914] + 1` | `stride` = 每行角点数 = 地图宽 + 1 |
| `[0x8759A0]` | `[0x87F918] + 1` | 角点行数 = 地图高 + 1 |

> `[0x87F90C..0x87F918]` 是一个标准的 `{X, Y, Width, Height}` 单元格矩形——
> `0x577A50` 那段「随机挑一格」的代码写得很直白：
> `X = [0x87F90C] + RandomRanged(0, [0x87F914]-1)`、
> `Y = [0x87F910] + RandomRanged(0, [0x87F918]-1)`。
> 注意它**不是** `0x42B1F0` 用的那个 `[0x87F8D4..0x87F8E0]`，两个矩形差 0x38。

角点 `(px, py)` 的项地址 = `[0xB0B6EC] + (stride*py + px) * 8`；
**格子 `(cx, cy)` 的四个角**按固定顺序取（后面所有函数都照这个顺序）：

| 角序号 | 角点坐标 | 等距方位 |
|---|---|---|
| 0 | `(cx, cy)` | 北 |
| 1 | `(cx+1, cy)` | 东 |
| 2 | `(cx+1, cy+1)` | 南 |
| 3 | `(cx, cy+1)` | 西 |

其中 `cx = cell.X - originX`、`cy = cell.Y - originY`。

每项 8 字节：

| 偏移 | 含义 |
|------|------|
| `+0x00` | `int` 角点高度（像素） |
| `+0x04` | `char` locked：该角点不许移动 |
| `+0x05` | `char` dirty：被 `0x6B3E60` / `0x6B3A80` 改过 |
| `+0x06` | `char` **只读不写**（见 §七 的怪癖 3） |
| `+0x07` | 填充 |

初始化循环只把 `+0x00` / `+0x04` / `+0x05` 清零，**`+0x06`/`+0x07` 保持 `new` 出来的垃圾值**。

### 3.2 每个角点的高度从哪来（含一处"凑合"回退）

对角点 `(px, py)`，先算出它"名义上"对应的格子 `base = (px + originX, py + originY)`，
然后按**固定顺序试 4 个候选格**，取第一个落在可用菱形里的：

```
base + (0, 0)  ->  base + (0,-1)  ->  base + (-1,-1)  ->  base + (-1, 0)
```

偏移表就写死在栈上（`local_c = 0xFFFF0000`、`local_8 = 0xFFFFFFFF`、`local_4 = 0x0000FFFF`，
按 `{int16 dx, int16 dy}` 解读）。循环最多推进 3 次，
**第 4 次出来的候选不再判菱形就退出循环**，随后由循环外那次判定兜底。

- 命中：`height = RampTable[cell->+0x11C][0] + (signed char)cell->+0x11B * 15`
  ——注意**只取该坡面表的第 0 个分量**（北角），不管这个角点在那个格子里其实是第几个角。
- 一个候选都不在菱形里：`height = 0`，并且 `locked = 1`。

### 3.3 locked 的判定

命中之后再看**共用这个角点的四个格子**：`(X, Y-1)`、`(X-1, Y-1)`、`(X-1, Y)`、`(X, Y)`
（`X/Y` 是 3.2 里的 `base`，不是最终命中的候选格）。
其中任意一个**落在可用菱形内、但"不可抬升"**，就把 `locked = 1`；
落在菱形外的格子直接跳过，不影响判定。

"可不可抬升"就是 `0x6B2520`：

```c
bool IsRaisable(CellClass* c) {          // 0x6B2520
    return InUsableDiamond(c->xy)                 // 0x5AC230
        && c->+0x44 == -1                         // 无 overlay
        && c->+0xE4 == 0
        && Work[c].+0x45 == 0                     // 阶段 ① 打的锚点位
        && (c->+0x38 >= [0xA8ED38] || c->+0x38 == 0xFFFF
            || TileTypes[c->+0x38]->+0x2E0 != 0); // 疑似 Morphable
}
```

前两个格子 `(X, Y-1)` / `(X-1, Y-1)` 是**内联展开**的 `IsRaisable`，
后两个 `(X-1, Y)` / `(X, Y)` 走真正的 `call 0x6B2520`——同一个谓词，编译器只内联了一半。

## 四、主循环：把高度场落到地形（无随机）

```
005a3645  movsx ecx, byte ptr [edi+0x11b]   ; cell->Level
005a3652  fild dword [esp+0x1c]
005a3656  fadd qword [esi+8]                ; + work->height
005a3659  fstp qword [esp+0x20]             ; target
005a365d  call 0x6b4100                     ; cur = 当前高度（四角最小值 / 15）
005a3666  fild dword [esp+0x1c]
005a366a  fsubr qword [esp+0x20]            ; target - cur
005a366e  call 0x7c5f00                     ; delta = _ftol(...)
005a3675  cdq / xor eax,edx / sub eax,edx   ; n   = abs(delta)
005a367a  xor edx,edx / setge dl / dec edx / and edx,-2 / inc edx
                                            ; dir = (delta >= 0) ? +1 : -1
005a368e  循环 n 次：
005a3692    call 0x6b4240                   ; mask = 该抬哪几个角
005a369f    call 0x6b3e60                   ; 抬一级（15 像素）
```

```c
for (int i = 0; i < side*side; ++i) {
    Work* w = &work[i];
    if (w->xy == 0) continue;
    CellClass* c = TryGetCellAt(&w->xy);
    double target = (double)(signed char)c->Level + w->height;
    int cur   = GetCurrentLevel(c);              // 0x6B4100
    int delta = (int)(target - (double)cur);     // _ftol，向零截断
    int dir   = (delta >= 0) ? +1 : -1;
    for (int n = abs(delta); n > 0; --n)
        RaiseCorners(dir, /*force=*/0, w->xy, PickCorners(c, dir));
}
```

四个细节：

1. `mask` 在**每一级**都重算（循环回跳点是 `0x5A368E`，在 `call 0x6B4240` 之前），
   因为上一级抬完之后四角高度已经变了。
2. `dir` 的三态被压成两态：`delta == 0` 时 `setge` 给出 `dir = +1`——但此时 `n = 0`，
   循环一次都不进，所以看不出来。
3. 传给 `0x6B3E60` 的坐标取自**工作记录的 `+0x00`**（packed XY），不是 `cell->+0x24`；
   两者相等，但复刻时别把它当成"当前格指针"。
4. **`0x6B3E60` 的返回值被完全忽略**。它在"抬完发现坡面不合法"时会整批回滚并返回 0，
   驱动循环却照样把剩下的 `n-1` 级抬完——每一级都撤销、每一级都白干。
   这是原版行为，照抄。

这套东西就是地图编辑器的「抬升地形」操作，**完全不消耗随机数**，
但要逐字节复刻地形就绕不开，下面四节逐个拆开。

## 五、`0x6B4100` / `0x6B4240`：读当前高度、选要动的角

### 5.1 `int GetCurrentLevel(CellClass* cell)`（`0x6B4100`，`__fastcall`）

```c
int h[4] = { 四角高度，按 §3.1 的 0/1/2/3 顺序 };
int mn = 1000;
for (i = 0; i < 4; ++i) if (h[i] < mn) mn = h[i];
if (IsRaisable(cell))          // 与 0x6B2520 逐条相同，此处内联
    return mn / 15;            // 有符号除法
return (signed char)cell->+0x11B;   // 不可抬升 -> 直接报当前 Level
```

`mn` 的初值 `1000` 只是个哨兵，四个角一定都会参与比较，实际取不到。

### 5.2 `int PickCorners(CellClass* cell, int dir)`（`0x6B4240`，`__fastcall`，`ecx=cell, edx=dir`）

返回 **4 位掩码，bit i 对应 §3.1 的角序号 i**：

```c
int h[4], locked[4];           // 四角，顺序同上
int mx = -1, mn = 1000;
for (i = 0; i < 4; ++i) { mx = max(mx, h[i]); mn = min(mn, h[i]); }

if (mx == mn)                  // 四角等高：全部未锁的角一起动
    return bits{ i : locked[i] == 0 };
if (dir > 0)                   // 抬：把低于最高角的角提上来
    return bits{ i : h[i] < mx && locked[i] == 0 };
else                           // 降：把高于最低角的角压下去
    return bits{ i : h[i] > mn && locked[i] == 0 };
```

> **"四角等高"分支是常态，不是边角情况。**（关掉了旧文档 §七 的未确认 6）
> 造山开始时整张地图是平的，第一次抬升任何一格都走这条路——四个角一起 +15，
> 于是格子整体升一级、地形仍然是平的。只有当邻格已经被抬过、四角出现高差时，
> 才会走 `dir` 那两条分支，把落后的角补齐成一个坡面。

## 六、`0x6B3E60`：抬/降一级（含撤销栈）

签名（`__fastcall`）：`bool Raise(int dir /*ecx*/, char force /*dl*/, CellStruct xy, uint mask)`。

### 6.1 掩码要先做一次位重排

```
006b3e9f  mov ebp, dword ptr [0x8759ac]
006b3ea5  mov ecx, eax        ; eax = mask
006b3ea7  mov edx, eax
006b3ea9  shr ecx, 1
006b3eab  and edx, 4
006b3eae  and ecx, 4
006b3eb1  shl edx, 1
006b3eb3  or  ecx, edx
006b3eb5  and eax, 3
006b3ebd  or  ecx, eax        ; ecx = (mask>>1 & 4) | ((mask&4)<<1) | (mask&3)
```

即 **bit2 与 bit3 互换**。原因是本函数遍历四个角点用的是**行优先**顺序
`(cx,cy) → (cx+1,cy) → (cx,cy+1) → (cx+1,cy+1)`，
而 `0x6B4240` 给出的掩码是**环形角序**（北东南西）。复刻时这一步不能省。

### 6.2 主体

```c
UndoStack.Clear();                       // 见 6.3，每次调用都重置
uint bit = 1;
for (int y = cy; y < cy + 2; ++y)
  for (int x = cx; x < cx + 2; ++x, bit <<= 1) {
      if (!(remapped_mask & bit)) continue;
      Entry* e = &lat[stride*y + x];
      if (e->locked) {
          if (force) continue;
          rollback(); return false;      // 撞到锁住的角就整批撤销
      }
      int nh = e->h + dir * 15;
      e->h = nh;
      if (nh < 0 || nh > 180) {          // 0xB4 = 12 级 x 15 像素
          e->h = nh - dir * 15;          // 越界：原样退回，不入撤销栈、不置 dirty
      } else {
          UndoStack.Add({ x, y, nh - dir*15 });
          e->dirty = 1;
          if (!CheckSlopes(x, y) && !force) { rollback(); return false; }
      }
  }
return true;
```

注意 `bit <<= 1` 在**内层循环末尾无条件执行**，掩码位没被选中的角也照样消耗一个位。

### ⚠️ 越界回退的分支不置 dirty

高度撞到 `[0, 180]` 边界时，代码只是把 `e->h` 减回去，
**既不入撤销栈也不置 `dirty`，也不调 `CheckSlopes`，更不返回失败**——
循环继续处理下一个角。所以"抬到顶"的格子会静默地只抬起一部分角。

### 6.3 撤销栈就是一个 `DynamicVectorClass`

对象在 `[0xB0B650]`，元素 12 字节 `{int x; int y; int oldHeight;}`：

| 地址 | 字段 |
|---|---|
| `0xB0B650` | vtable |
| `0xB0B654` | `Vector`（元素数组指针） |
| `0xB0B658` | `VectorMax`（容量） |
| `0xB0B65C` | `IsValid` |
| `0xB0B65D` | `IsAllocated` |
| `0xB0B660` | `ActiveCount` |
| `0xB0B664` | `GrowthStep` |

入栈就是标准的 `DynamicVectorClass::Add`：
`ActiveCount < VectorMax` 直接写，否则
`(IsAllocated || VectorMax == 0) && GrowthStep > 0` 时调 `vtable[8](VectorMax + GrowthStep, NULL)` 扩容。

`0x6B3E60` 一进来就**先 free 掉整个数组**（`ActiveCount = VectorMax = IsAllocated = 0`），
所以撤销范围只限本次调用的这一级。

### ⚠️ 回滚只还原高度，不还原 dirty

```c
void rollback() {
    for (int i = ActiveCount - 1; i >= 0; --i)
        lat[stride * rec[i].y + rec[i].x].h = rec[i].oldHeight;   // 只写 +0x00
    ActiveCount = 0; free(Vector); ...
}
```

`dirty`（`+0x05`）在回滚后**仍然是 1**。而 §八 的落地遍历正是按 `dirty` 挑格子的——
于是一次失败的抬升会让相关格子被"重新刷一遍砖"，尽管高度根本没变。照抄。

## 七、`0x6B3A80`：坡面合法性（递归，会顺手改邻居）

签名（`__fastcall`）：`bool CheckSlopes(int dir /*ecx*/, char force /*dl*/, int x, int y)`。
`dir` / `force` 是从 `0x6B3E60` 直接透传下来的寄存器参数，递归时也原样带着。

规则一句话：**8 邻域内任意两个相邻角点的高度差不得超过 15 像素（1 级）**。

```c
int h0 = lat[stride*y + x].h;
for (int dy = -1; dy <= 1; ++dy)
  for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= stride || ny >= rows) {
          if (!force) return false;            // 越出格网边界即判非法
          continue;
      }
      Entry* e = &lat[stride*ny + nx];
      if (e->locked && !(force && e->+0x06)) { // 锁住的角只能验，不能改
          if (abs(e->h - h0) > 15) return false;
          continue;
      }
      if (abs(e->h - h0) <= 15) continue;
      if (dir == 1) {                          // 抬：把太低的邻角拉上来
          if (e->h < h0) { Undo.Add({nx,ny,e->h}); e->h = h0 - 15; e->dirty = 1; }
      } else {                                 // 降：把太高的邻角压下去
          if (e->h > h0) { Undo.Add({nx,ny,e->h}); e->h = h0 + 15; e->dirty = 1; }
      }
      if (!e->locked && !CheckSlopes(nx, ny) && !force) return false;
  }
return true;
```

四条要点：

1. **遍历顺序是 x 内层、y 外层，`-1..+1`，跳过 (0,0)**——递归会按这个顺序展开，
   顺序不同就会得到不同地形。
2. **越界即非法**：格网边缘那一圈角点（`x==0`、`y==0`、`x==stride-1`、`y==rows-1`）
   只要被 `CheckSlopes` 碰到就必然返回 false。因为可用菱形离矩形边界还有距离，
   正常路径上碰不到，但这是硬边界，复刻时不能"宽容处理"。
3. **邻角被拉动时不夹 `[0, 180]`**。`0x6B3E60` 主循环有边界检查，
   这里的连带调整没有——理论上能把角点推到负数或超过 180。
4. `|diff| > 15` 但方向"不对"（抬升时邻角反而更高）的情况**不改高度，但仍然递归**。

## 八、`0x6B3850`：把角点格网落回砖号与坡面（无随机）

遍历全图格子；**只处理四角里至少有一个 `dirty` 的格子**：

```c
for (CellClass* c : MapCells) {
    if (!(corner[0].dirty || corner[1].dirty || corner[2].dirty || corner[3].dirty))
        continue;
    int h[4] = { 四角高度 };                 // §3.1 的 0/1/2/3 顺序
    int mn = 1000, mx = 0;
    for (i = 0; i < 4; ++i) { mn = min(mn, h[i]); mx = max(mx, h[i]); }
    if (mx - mn >= 16) continue;             // 高差超过一级：这格放弃，砖号原样
    if (!IsRaisable(c)) continue;            // 内联的 0x6B2520
    c->+0x11B = (signed char)(mn / 15);      // Level
    for (i = 0; i < 4; ++i) h[i] -= mn;      // 归一化到 0 基
    for (int k = 0; k < 19; ++k)             // 线性查 RampTable
        if (memcmp(RampTable[k], h, 16) == 0) {
            c->+0x11C = (char)k;
            c->+0x38  = (k == 0) ? ClearTile : (RampBase - 1 + k);
            break;
        }
}
free([0xB0B6EC]);  [0xB0B6EC] = NULL;        // 格网用完即销毁
```

- `mx` 的初值是 **0 而不是 -1**（`0x6B4240` 用的是 -1）。高度恒非负，所以没差别。
- 查表**没找到就什么都不做**：`+0x11C` 和 `+0x38` 保留原值，`+0x11B` 却已经被改了。
  正常情况下 `mx-mn < 16` 且四角归一后一定是 `{0, 15}` 的组合，表里全覆盖；
  但 §七 那条"连带调整不夹边界"的路径理论上能造出表外形状。
- 两个砖组全局：`ClearTile = [0xAA10B0]`，`RampBase = [0xABC1D8]`——
  后者是本次新查实的，theater ini `[General] RampBase=`
  （取证：`0x545CEF` 的 `cmp edi, [esp+0x160]` 对上 `0x545740` 那次
  `ReadInt("General", "RampBase", -1)`）。已补进 `memory/data/rmg/tileset_globals.txt`。

### `RampTable`（`0x83FF18`，19 个 `int[4]` 指针）

单位是像素，除以 15 就是级数；四个分量按 §3.1 的**北 / 东 / 南 / 西**顺序。

| `+0x11C` | 表项地址 | 北 | 东 | 南 | 西 | 形状 |
|---|---|---|---|---|---|---|
| 0 | `0xB0B6DC` | 0 | 0 | 0 | 0 | 平地（`.bss`，全零） |
| 1 | `0x83FDD8` | 0 | 15 | 15 | 0 | 半坡 |
| 2 | `0x83FDE8` | 0 | 0 | 15 | 15 | 半坡 |
| 3 | `0x83FDF8` | 15 | 0 | 0 | 15 | 半坡 |
| 4 | `0x83FE08` | 15 | 15 | 0 | 0 | 半坡 |
| 5 | `0x83FE18` | 0 | 0 | 15 | 0 | 单角凸 |
| 6 | `0x83FE28` | 0 | 0 | 0 | 15 | 单角凸 |
| 7 | `0x83FE38` | 15 | 0 | 0 | 0 | 单角凸 |
| 8 | `0x83FE48` | 0 | 15 | 0 | 0 | 单角凸 |
| 9 | `0x83FE58` | 0 | 15 | 15 | 15 | 单角凹 |
| 10 | `0x83FE68` | 15 | 0 | 15 | 15 | 单角凹 |
| 11 | `0x83FE78` | 15 | 15 | 0 | 15 | 单角凹 |
| 12 | `0x83FE88` | 15 | 15 | 15 | 0 | 单角凹 |
| 13 | `0x83FE98` | 0 | 15 | 30 | 15 | 双级坡 |
| 14 | `0x83FEA8` | 15 | 0 | 15 | 30 | 双级坡 |
| 15 | `0x83FEB8` | 30 | 15 | 0 | 15 | 双级坡 |
| 16 | `0x83FEC8` | 15 | 30 | 15 | 0 | 双级坡 |
| 17 | `0x83FED8` | 0 | 15 | 0 | 15 | 鞍形 |
| 18 | `0x83FEE8` | 15 | 0 | 15 | 0 | 鞍形 |

> 表项 0 指向 `.bss` 的 `0xB0B6DC`（紧挨着格网指针 `0xB0B6EC`），
> 全 exe 没有任何代码写它——就是个 `static int flat[4];`，恒为 `{0,0,0,0}`。
> 表项 1–18 在 `.data`，按 16 字节连续排布。
> 循环终止条件是硬编码的 `ptr < 0x83FF64`，正好 19 项。

§5.2 里"四角等高"分支之所以是常态，配合这张表就说得通了：
平地格子被整体抬起后归一化仍是 `{0,0,0,0}`，命中表项 0，砖号写回 `ClearTile`。

## 九、收尾：压平 2x2 坡块（无随机）

再走一遍全图格子，检查以当前格为左上角的 2x2 方块
（偏移 `(0,0) (1,0) (1,1) (0,1)`，立即数 `0, 1, 0x10001, 0x10000`）：

| 触发 | 四格 `+0x11C` 要求 | 处理 |
|------|-------------------|------|
| `cell->+0x11C == 5` | `5, 6, 7, 8` | 四格压平，`Level += 0` |
| `cell->+0x11C == 0x0B` | `11, 12, 9, 10`（表在栈上 `{0xB,0xC,9,0xA}`） | 四格压平，`Level += 1` |

压平 = `+0x38 = 0xFFFF`（空砖）、`+0x11A = 0`、`+0x11C = 0`（无坡面）。

## 十、本阶段随机数账

| 取数点 | 触发条件 | 次数 |
|--------|---------|------|
| `0x598003`（`UniformUnit`，经 `NormalDeviate`） | `Ruggedness >= 10` | 每个 `xy != (0,0)` 的工作记录 **2 次 `TruncNormal`**，每次含拒绝重抽；缓存命中时 0 个随机数 |

## 十一、抬升子系统的原版怪癖清单

抬升子系统一处随机数都不取，但下面这些"错"会实打实地改变输出地形，
按硬性规范 #6 必须原样复刻：

| # | 位置 | 表现 |
|---|------|------|
| 1 | `0x6B2A70` §3.2 | 角点高度只取 `RampTable[ramp][0]`（北角），与该角点在候选格里的真实方位无关；候选格还是"往左上凑"的 4 选 1 |
| 2 | `0x6B3E60` §6.2 | 高度撞 `[0, 180]` 边界时静默退回：不入撤销栈、不置 `dirty`、不验坡、不报错 |
| 3 | `0x6B3A80` §七 | 读了角点项的 `+0x06`，而 `0x6B2A70` 的初始化**从没写过这个字节**——`operator new` 的垃圾值。所幸只在 `force != 0` 时才读，而 RMG 全程 `force == 0`，走不到 |
| 4 | `0x6B3E60` 回滚 | 只还原 `+0x00` 高度，`dirty` 留在 1 → §八 会去刷一批高度根本没变的格子 |
| 5 | `0x5A35F0` 主循环 | 忽略 `0x6B3E60` 的返回值，抬升失败后照样把剩下的级数抬完，每级都白干一遍 |
| 6 | `0x6B3A80` | 连带调整邻角时不夹 `[0, 180]`，可能把角点推出合法范围 |
| 7 | `0x6B3850` | `RampTable` 查不到形状时静默跳过，但 `+0x11B`（Level）已经改了——地形高度与砖号可能对不上 |

## 十二、未确认

1. `TileTypeClass +0x2E0` 的键名（疑似 `Morphable`）
2. `CellClass +0xE4`、`+0x44`、`+0x11A` 的准确语义
3. 角点项 `+0x06` 的原意（只在 `0x6B3A80` 的 `force` 分支被读，
   同编译单元的另外几个函数 `0x6B24CE` / `0x6B27ED` / `0x6B30B0` 疑似地图编辑器路径，
   可能在那边被写；RMG 路径上无影响）
4. `[0x87F90C..0x87F918]` 与 `[0x87F8D4..0x87F8E0]` 两个矩形的官方字段名——
   已实锤前者是 `{X, Y, Width, Height}` 且格网按它分配，名字未确认
5. 表项 0 的 `.bss` 位置 `0xB0B6DC` 是否会被同编译单元的编辑器路径写脏——
   RMG 单进程一次生成里恒为 `{0,0,0,0}`

## 十三、复刻状态：⚠️ 高度场已落 C++ 并逐位对拍，其余仍是文档

`code/rewrite/rmg/height_field.{h,cpp}`（ctest 名 `height_field`）落了 §二 的全部内容：

| 本文的东西 | 原版地址 | C++ |
|---|---|---|
| 三个尺度 + `<= 10` 出口 | `0x5A2F59`-`0x5A2FA8` | `GenerateHeightField` 开头 |
| 主循环（左/上邻平均 + 梯度取符号 + 锚点重置） | `0x5A2FC2`-`0x5A3383` | 同上 |
| `TruncNormal` | `0x5A3215` / `0x5A32ED` | `TruncNormal` |
| 取整（远离零） | `0x5A3392`-`0x5A33E2` | `RoundHalfAwayFromZero` |

工作记录的 `+0x08` / `+0x10` / `+0x45` 三项加进了 `PaintWorkCell`
——它们与涂块用的 `+0x38` / `+0x3C` 本来就住在同一条 0x50 字节记录里。
`+0x45` 按偏移命名（`flag45`），因为阶段 6 的 `0x5A1FB0` 和本阶段的
`0x5A33F0` 拿它当两件不同的事用，语义未定名。

**对拍强度：黄金对拍，逐位一致。** 基准由
`code/randmap/dump_height_field_golden.py` 用 Unicorn 跑 `0x5A2F50` 真身取得
（FPCW=0x0E7F），存档 `memory/data/rmg/height_field_golden.txt`；
9 个用例覆盖 `Ruggedness` 0 / 9 / 10 / 60 / 200 / 400 / 1000 / 5000 / 负数，
带/不带锚点，逐格比 `height` 与 `slope` 的**完整 64 位**（不是近似比较）。

顺带验证了一条一直悬着的事：`NormalDeviate` 里的 `ChopLog`（我们改用
「精确 ln 向零截断」而不是本机 `fyl2x`，理由见 [fast-math.md](fast-math.md) §四）
在这 9 个用例、上万次抽样里与原版 `fyl2x` **一次都没分岔**。

§一（锚点）、§三~§九（角点抬升子系统）**还没落 C++**——它们要吃砖表与
`CellClass`，得等 `.tmp` 装载器齐了才做得起来。
