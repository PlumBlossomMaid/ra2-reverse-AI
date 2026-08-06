# 阶段 9：Creating hills——造山

> 入口 `0x5A35F0`（`__thiscall`，`ecx = MapSeedClass*`）。
> RNG 总账里本阶段只有 1 处取数点 `0x598003`，即 `NormalDeviate` 内部的
> `UniformUnit`——**全部随机性集中在 `0x5A2F50` 这一个高度场生成器里**，
> 每个可用格子抽 2 次正态。
> 取证：`memory/data/decomp/rmg_hills.txt`、`rmg_hills2.txt`、`rmg_hills3.txt`。

```
005a35f9  call 0x5a33f0     ; ① 打「不许动」的锚点
005a35fe  call 0x5a2f50     ; ② 生成连续高度场（唯一取随机数的地方）
005a3605  call 0x6b2a70     ; ③ 重建角点高度格网（无随机）
          <主循环>          ; ④ 把高度场落到地形上（无随机）
005a36c1  call 0x6b3850     ; ⑤ 依角点格网刷砖与坡面（无随机）
          <2x2 收尾遍>      ; ⑥ 压平不合法的 2x2 坡块（无随机）
```

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
if (baseSlope < 0.025) return;                  // 即 Ruggedness < 10 时完全不造山
```

**`Ruggedness < 10` 时本阶段一个随机数都不取。**（`MapSeedClass +0x44 = Ruggedness`，
见 `docs/random-map/parameters.md`）

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

分配 `(w+1)*(h+1)` 个 8 字节项到 `[0xB0B6EC]`，
矩形取自 `[0x87F90C..0x87F918]`，尺寸缓存在 `[0x8759A0..0x8759AC]`。

每项：

| 偏移 | 含义 |
|------|------|
| `+0x00` | `int` 角点高度（像素）= `RampTable[cell->+0x11C] + cell->Level * 15` |
| `+0x04` | `char` locked：该角点不许移动 |
| `+0x05` | `char` dirty 标记（`0x6B3E60` 里置 1） |

`RampTable = [0x83FF18]`，按 `cell->+0x11C`（坡面类型）索引。
角点被判 locked 的条件：越界，或四周任一格 `0x6B2520` 判定为「不可抬升」——
即 `+0x44 != -1`（有覆盖物）、`+0xE4 != 0`、**工作记录 `+0x45` 锚点位被置**、
或砖的 `TileTypeClass->+0x2E0` 为假。

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

- `0x6B4100(cell)`：取 4 个角点高度的最小值除以 15；若该格「不可抬升」则直接返回
  `cell->Level`
- `0x6B4240(cell, dir)`：4 个角点里，`dir > 0` 时选**低于最大值**的角、
  `dir <= 0` 时选**高于最小值**的角；四角等高时选全部未 locked 的角。返回 4 位掩码
- `0x6B3E60(dir, force, xy, mask)`：把选中的角点 `+= dir * 15`，
  夹在 `[0, 180]`（= 12 级 x 15 像素）内；每步压入撤销栈
  `[0xB0B654]`（每项 12 字节 `{x, y, oldHeight}`），
  改完调 `0x6B3A80` 验坡面合法性，**不合法且 `force == 0` 就整批回滚**

这套东西就是地图编辑器的「抬升地形」操作，**完全不消耗随机数**，
但要逐字节复刻地形就绕不开——单独列为后续任务。

## 五、收尾：压平 2x2 坡块（无随机）

再走一遍全图格子，检查以当前格为左上角的 2x2 方块
（偏移 `(0,0) (1,0) (1,1) (0,1)`，立即数 `0, 1, 0x10001, 0x10000`）：

| 触发 | 四格 `+0x11C` 要求 | 处理 |
|------|-------------------|------|
| `cell->+0x11C == 5` | `5, 6, 7, 8` | 四格压平，`Level += 0` |
| `cell->+0x11C == 0x0B` | `11, 12, 9, 10`（表在栈上 `{0xB,0xC,9,0xA}`） | 四格压平，`Level += 1` |

压平 = `+0x38 = 0xFFFF`（空砖）、`+0x11A = 0`、`+0x11C = 0`（无坡面）。

## 六、本阶段随机数账

| 取数点 | 触发条件 | 次数 |
|--------|---------|------|
| `0x598003`（`UniformUnit`，经 `NormalDeviate`） | `Ruggedness >= 10` | 每个 `xy != (0,0)` 的工作记录 **2 次 `TruncNormal`**，每次含拒绝重抽；缓存命中时 0 个随机数 |

## 七、未确认

1. `0x6B3850` 的细节（依角点格网反查砖号与坡面类型 `+0x11C`）——只确认了无随机
2. `0x6B3A80` 的坡面合法性判据
3. `RampTable = [0x83FF18]` 的具体内容（`+0x11C` 的取值域与像素高度映射）
4. `TileTypeClass +0x2E0` 的键名（疑似 `Morphable`）
5. `CellClass +0xE4`、`+0x44`、`+0x11A` 的准确语义
6. `0x6B4240` 里「四角等高」分支返回全部未 locked 角，与 `dir` 无关——
   是否真会在 RMG 路径上触发，未验证
