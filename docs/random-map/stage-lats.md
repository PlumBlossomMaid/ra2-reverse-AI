# 阶段 10：Creating LATs, rocks etc

日志字符串 `0x82BE10 "RMG: Creating LATs, rocks etc\n"`，打印点 `0x599215`。
**18 个取数点**，是整条流水线里取数点最密的一段。

## 分派：温带 / 雪地两条互斥路径

调度代码在 `0x59921B-0x5992AB`（`iVar9` = MapSeedClass）：

```c
if (seed->Theater == 0) {            // +0x38 == 0，温带（唯一另一个可能值是 1 = 雪地）
    FUN_005a38c0();                  // 按 Vegetation 播三张概率图，0 取数
    FUN_005a3ae0();                  // 主体，10 取数
} else {
    // 雪地：内联铺常量：所有工作格 +0x28 = 0.005、+0x20 = 0.001
    for (cell in MapClass) {
        work = &Work[cell.Y * side + cell.X];
        work->+0x28 = 0.005;         // [0x7ED7F0]
        work->+0x20 = 0.001;         // [0x7ED7F8]
    }
    FUN_005a4280();                  // 简化版主体，2 取数
}
```

> **“其它 theater”实际就是雪地一种。**
> RMG 对话框只提供**温和 / 雪地**两个气候（玩家实机确认），
> 代码侧也对得上：rules.ini 的 RMG 配置键**只有 `Temperate*` 与 `Snow*` 两套**
> （`TemperateAmbientLight`/`SnowAmbientLight`、`Temperate/SnowAmbientRed|Green|Blue`、
> `Temperate/SnowOrePatchLamps`，读取点 `0x59843E`-`0x598871`），
> 没有 Urban / Desert / NewUrban / Lunar 的对应项。
> 而 `0x5997C0` 处 `"TEMPERATE" + Theater * 0x70` 索引的 `TheaterTypeClass` 表（`0x7E1B78`）
> 确实包含全部 6 个 theater，但 RMG 只会传进去 0 / 1。
> 所以 **`0x5A4280` 就是雪地分支**，不是什么通用兼容路径。

即：**温带的三张概率图是按 `Vegetation` 算出来的，雪地直接写死两个常量**
（而且雪地那条路根本不写 `+0x18`，见 §三）。

工作记录三个概率槽的用途（本阶段实锤）：

| 槽位 | 落地成什么 | 温带（Theater=0） | 雪地（Theater=1） |
|---|---|---|---|
| `+0x18` | RoughTile（碎石 LAT） | `0.02 * veg`，岸边 `0.02 * veg * 10` | **不写**（沿用上一阶段残值） |
| `+0x20` | GreenTile（草地 LAT） | `0.005`，岸边 `0.0` | `0.001` |
| `+0x28` | SandTile（沙地 LAT） | `0.005` | `0.005` |

其中 `veg = Vegetation * 0.01`（`fild [esi+0x5c]; fmul [0x7E3808]=0.01`）。

## 一、`0x5A38C0`：按植被播概率图（0 取数）

遍历所有格子，先判 `IsShorePiece`（`0x4865B0`，`+0x38 ∈ [ShorePieces, +42)`）：

- **非岸格**：只有当 `work->+0x28 == 0.0`（`fcomp [0x7E2800]=0.0`）时才写，
  即"还没被别的阶段占过"才播种：

  ```
  work->+0x28 = 0.005;             // [0x7ED7D8]
  work->+0x18 = 0.02 * veg;        // [0x7ED7E0] = 0.02
  work->+0x20 = 0.005;             // [0x7ED7E8]
  ```

- **岸格**：跑一圈 `dx,dy ∈ [-2,+3)` 的 5×5 邻域，每个落在可用菱形内的邻居写：

  ```
  work->+0x28 = 0.005;
  work->+0x20 = 0.0;
  work->+0x18 = 0.02 * veg * 10.0; // [0x7E44A8] = 10.0
  ```

  意思显然是"岸边一圈碎石概率放大 10 倍"。

### ⚠️ 原版 bug：5×5 邻域写的全是中心格

写目标指针 `esi` 在**进入邻域循环之前**就算好了，而且用的是**被迭代格自己**的 XY：

```
005a3909  movsx esi, word ptr [esp + 0x16]     ; esi = cell.Y
005a390e  imul  esi, dword ptr [0x89c2dc]      ; * side
005a3915  movsx eax, ax                        ; eax = cell.X
005a3918  add   esi, eax
005a391a  lea   esi, [esi + esi*4]             ; * 5
005a391d  shl   esi, 4                         ; * 16  -> * 0x50
005a3920  add   esi, edx                       ; + [0xABED10]
```

循环体（`0x5A3999-0x5A39C2`）写的是 `[esi+0x28]` / `[esi+0x20]` / `[esi+0x18]`——
`esi` 在 `0x5A3966-0x5A3997` 的边界判定里一次都没被重算。
两层循环变量 `edx`(x') / `ecx`(y') **只参与 `InUsableDiamond` 的四个不等式**，
不参与地址计算。

后果：本该"岸边 5×5 一片放大"，实际是**对中心格重复写 25 次同一个值**
（最多 25 次，落在菱形外的会跳过）。所以岸边碎石只多在岸格自己身上，不扩散。

按硬性规范 #6，**这个 bug 必须原样复刻**：等价于
"若该岸格自身落在可用菱形内，就给它自己写一遍放大值"（重复写幂等，写 1 次和 25 次同果），
若整个 5×5 全在菱形外则一次都不写。注意判定用的是**邻居坐标**，
所以"写不写"仍然依赖 5×5 里是否至少有一个邻居在菱形内——**不能**简化成只判中心格。

## 二、`0x5A3AE0`：温带主体（10 个取数点）

四步：**清 field id → 铺三种 LAT → `SetupLAT` 归一 → 撒树林补丁 → 撒石头**。

### 2.1 清 `work->+0x3C`（`0x5A3B0D-0x5A3B40`）

全图工作记录的 `+0x3C`（区域/田块 id）清零，给后面的补丁生长复用。无取数。

### 2.2 三次 `PickRange(20,40)`（`0x5A3B58` / `0x5A3B88` / `0x5A3BB8`）

三个内联区间随机，**在遍历格子之前一次性抽好**，整张地图共用：

```
005a3b58  mov  ecx, 0xabe890
005a3b5d  call 0x65c780                  ; Randomizer::Random
005a3b6a  fild qword ptr [esp + 0x48]    ; 无符号：高 32 位补 0
005a3b6e  fmul qword ptr [0x7edad0]      ; 21/(2^32-1)
005a3b74  fadd qword ptr [0x7e44c8]      ; + 20.0
005a3b7a  call 0x7c5f00                  ; _ftol
005a3b7f  cmp  eax, 0x28
005a3b82  ja   0x5a3b58                  ; 无符号 > 40 就重抽
```

即 `PickRange(20, 40)` 的**内联展开**（`+lo` 在 `_ftol` 之前，与 `0x598030` 同惯用法），
均匀落在 **[20, 40]** 共 21 个值。三个值分别是三种 LAT 的**中心尺寸**
（下面记作 `pick_rough` / `pick_sand` / `pick_green`）。

### 2.3 逐格铺 LAT（每格最多 3 次抛硬币）

格子门槛（`0x5A3C18-0x5A3C4A`，五个条件全满足才处理）：

```c
IsPlain(cell)              // 0x486380: +0x38 == 0 || +0x38 == 0xFFFF
&& cell->+0x11C == 0       // 无坡道
&& cell->+0x44  == -1      // 无 overlay
&& cell->+0xE4  == 0       // 无 tiberium/占位
&& work->+0x45  == 0       // 未被锁定/锚定
```

三次机会依次是碎石、沙地、草地，**任意一次命中就跳出本格**（`jmp` 到迭代器 next）：

| 次序 | 取数点 | 概率槽 | TileSet 全局 | 命中后 |
|---|---|---|---|---|
| 1 | `0x5A3C53` | `work->+0x18` | `RoughTile [0xABC2B8]` | 铺完即下一格 |
| 2 | `0x5A3D3F` | `work->+0x28` | `SandTile [0xABB104]` | 铺完即下一格 |
| 3 | `0x5A3E1B` | `work->+0x20` | `GreenTile [0xAA0E18]` | 铺完即下一格 |

抛硬币惯用法（三处完全一致）：

```
005a3c69  call 0x65c780                  ; Random
005a3c76  fild qword ptr [esp + 0x50]    ; 无符号
005a3c7a  fmul qword ptr [0x7ed898]      ; 1/(2^32-1)
005a3c80  fcomp qword ptr [ebp + 0x18]   ; vs work->prob
005a3c85  test ah, 1                     ; C0: st < mem
005a3c88  je   <下一次机会>              ; 不小于就不铺
```

**注意方向**：`r < prob` 才铺。三次机会是**顺序回落**的，所以实际落地概率是
`p₁`、`(1-p₁)p₂`、`(1-p₁)(1-p₂)p₃`——**碎石优先级最高**。
同样地，**失败的抛硬币也消耗随机数**：一格最少 1 次、最多 3 次，随概率图逐格漂移。

命中之后抽尺寸——一次**截断正态**，`mu` 就是 2.2 抽好的那个整数：

```
005a3c8e  fild dword ptr [esp + 0x1c]        ; mu = (double)pick
005a3c92  mov  [esp+0x30], 0 / 0x40340000    ; sigma = 20.0（立即数拼双精度）
005a3ca6  fld  mu; fsub sigma
005a3cb0  fcomp qword ptr [0x7edac8]         ; vs hi = 80.0
005a3cb8  test ah, 0x41 / je  -> 回退         ; mu - sigma > hi ?
005a3cbd  fld  mu; fadd sigma
005a3cc7  fcomp qword ptr [0x7e3cc8]         ; vs lo = 4.0
005a3ccf  test ah, 1  / je  -> 跳过回退       ; mu + sigma < lo ?
005a3cd4  sigma = 38.0; mu = 42.0            ; 回退分支
005a3cf4  do { v = NormalDeviate()*sigma + mu; } while (v < 4.0 || v > 80.0);
```

即 `TruncNormal(mu = pick, sigma = 20.0, lo = 4.0, hi = 80.0)`。

两个细节值得记：

1. **回退分支的 `sigma`/`mu` 被编译期常量折叠**了。`TruncNormal` 的通用回退是
   `sigma = (hi-lo)*0.5; mu = sigma + lo;`，这里 `lo/hi` 都是字面量，
   于是直接写死 `38.0` / `42.0`——与阶段 9 手写展开的形式一致，可互相印证。
2. **回退分支不可达**：`mu ∈ [20,40]`、`sigma = 20`，
   故 `mu-sigma ∈ [0,20] ≤ 80`，`mu+sigma ∈ [40,60] ≥ 4`。复刻时保留即可，不必特判。

落地调用（`0x5A3D28-0x5A3EF7`，三条分支共用尾部）：

```c
FUN_005A4B60(/*this=*/cell, tileSetBase, _ftol(v), cellIndex, 0);
//                                                  ^^^^^^^^^ = cell.Y * 512 + cell.X
```

`cellIndex` 由 `shl esi, 9`（`0x5A3C64`）算出——**这里是写死的 512**，
不是工作数组用的 `[0x89C2DC]`，两者不要混。

### 2.4 `CellClass::SetupLAT` 全图归一（0 取数）

`0x5A3F19-0x5A3F3F` 重置迭代器再走一遍全图，对每格调 `0x47CA80`，
把上面涂出来的 LAT 块按邻接关系换成正确的过渡片。无随机。

### 2.5 树林补丁：预算 + 最多 100 轮生长（2 取数点）

预算公式（`0x5A3F41-0x5A3F61`）：

```
005a3f41  fild dword ptr [esi + 0x64]    ; Width
005a3f44  fmul qword ptr [0x7e3860]      ; x 0.1
005a3f4a  fadd qword ptr [0x7edac0]      ; + 0.7
005a3f50  fild dword ptr [esi + 0x5c]    ; Vegetation
005a3f53  fmul qword ptr [0x7e3808]      ; x 0.01
005a3f59  fmulp st(1), st(0)
005a3f5b  fimul dword ptr [esi + 0x2fc]  ; x MaxTrees（rules.ini [General]）
005a3f61  call 0x7c5f00                  ; _ftol
```

即 `budget = (int)((Width*0.1 + 0.7) * (Vegetation*0.01) * MaxTrees)`。
`budget <= 0` 直接跳过整段。

外层循环上限 **100 轮**（`cmp [esp+0x14], 0x64; jge` 退出），每轮：

1. **挑一格**（`0x5A3F8E`）：调**外联的** `PickRange(0, side*side-1)`（`0x598030`，
   取数点 `0x59805E`），要求 `work[idx].xy != CellStruct(0,0)`（`0x50E470` 比较），
   再要求 `IsPlain(cell)`；`tries` 超过 200 就**放弃挑选、直接用 `CellStruct(0,0)`**
   （`0x5A3FCC cmp ebp, 0xc8; jg 0x5a3ff4`）——原版怪癖，照抄。
   `tries` 在**每轮开头**清零（`0x5A3F81 xor ebp, ebp`），重试时从 `0x5A3F83` 回跳，不清。

2. **抽密度**（`0x5A400B`）：`prob = TruncNormal(mu=0.2, sigma=0.1, lo=0.05, hi=0.4)`，
   常量 `[0x7E3548]=0.2` `[0x7E3860]=0.1` `[0x7E8AE8]=0.05` `[0x7E3550]=0.4`。
   **无回退分支**——编译器把不可达的回退整个删了（`0.2±0.1` 恒在 `[0.05,0.4]` 内）。

3. **抽尺寸**：`size = _ftol(TruncNormal(mu=25.0, sigma=10.0, lo=10.0, hi=35.0))`，
   常量 `[0x7EDAB8]=25.0` `[0x7E44A8]=10.0` `[0x7EDA10]=35.0`。同样没有回退分支。

4. `budget -= FUN_005A45E0(cell, size, prob);` —— 生长函数自带 3 个取数点（见 §四）。

循环条件是 `budget > 0 && round < 100`，两个都在**每轮开头**判。

### 2.6 撒石头 / overlay（3 取数点）

先算一个"地图规模"数（`0x42B1F0`）：

```
0042b1f0  mov  eax, dword ptr [0x87f8e0]     ; MapRect.Height
0042b1f5  add  eax, 4
0042b1f8  imul eax, dword ptr [0x87f8dc]     ; * MapRect.Width
0042b1ff  shl  eax, 1                        ; * 2
```

`n = (Height + 4) * Width * 2`。然后：

```c
count = PickBelow(n/200 + 1);     // 0x5A40F0，有符号除法（shr 31 / add 修正）
                                  // 拒绝条件 cmp ebp, esi / ja  -> count ∈ [0, n/200]
placed = 0; attempt = 0;
if (count*5 <= 0) return;
while (placed < count && attempt < count*5) {
    do { idx = PickBelow(side*side); }        // 0x5A4157
    while (work[idx].xy == CellStruct(0,0));
    cell = TryGetCellAt(&work[idx].xy);
    attempt++;
    if (cell->+0x44 != -1) continue;          // 已有 overlay，不计入 placed
    if (IsClearToSandLat(cell))               // 0x486790
        cell->+0x44 = PickBelow(5) + 0xA8;    // 0x5A41DE
    else if (IsPlain(cell) || IsGreen(cell))
        cell->+0x44 = PickBelow(5) + 0xAD;    // 0x5A4221
    cell->+0x11E = 0;
    cell->RecalcAttributes(-1);               // 0x47D2B0
    placed++;
}
```

`0x486790` 是新实锤的谓词：`+0x38 ∈ [ClearToSandLat, +16)`，
TileSet 全局 `[0xAA0E24]`（映射见 `memory/data/rmg/tileset_globals.txt`）。
沙地 LAT 上撒 `0xA8..0xAC`，普通/绿地上撒 `0xAD..0xB1`，各 5 个变体。

三处 `PickBelow(5)` 的比例常量是**编译期折叠好的** `[0x7EDAB0] = 5/(2^32-1)`，
只有一次乘法——和通用 `PickBelow(n)` 的"两次乘法"形态不同，复刻时**别自作主张合并或拆开**。

### ⚠️ 原版怪癖：第三类格子白占一个名额

`else if (IsPlain || IsGreen)` 两个都不成立时，代码 `je 0x5A424F` 跳过 `+0x44` 的赋值，
但**仍然**执行 `+0x11E = 0`、`RecalcAttributes(-1)` 和 `placed++`。
也就是说：一个既不是沙 LAT、也不是平地/绿地、却又恰好 `+0x44 == -1` 的格子，
会**白白吃掉一个石头名额**，还被无谓地重算一次属性。照抄。

## 三、`0x5A4280`：雪地主体（2 个取数点）

结构是 §二的**削减版**——只有一种 LAT、没有石头：

1. 清全图 `work->+0x3C`。
2. 逐格：门槛 `IsPlain(cell) && cell->+0x11C == 0 && work->+0x45 == 0`
   （比温带少了 `+0x44 == -1` 和 `+0xE4 == 0` 两条）。
   一次抛硬币（`0x5A434D`）对 `work->+0x28`（= 0.005，见分派段），命中则
   `TruncNormal(mu=20.0, sigma=15.0, lo=4.0, hi=60.0)` → `_ftol` →
   `FUN_005A4B60(cell, RoughTile[0xABC2B8], v, cellIdx, 0)`。
   常量：`[0x7E44C8]=20.0` `[0x7ECE60]=15.0` `[0x7E3CC8]=4.0` `[0x7E1728]=60.0`。
   **这里的 `mu` 是写死的 20.0**，不像温带那样先 `PickRange(20,40)` 抽一个。
3. `SetupLAT` 全图归一。
4. 树林补丁预算 + 最多 100 轮，与 §2.5 **逐字节同构**：同样的
   `PickRange` 挑格 + `tries > 200 → (0,0)` 怪癖、同样的
   `TruncNormal(0.2, 0.1, 0.05, 0.4)` 和 `TruncNormal(25, 10, 10, 35)`、
   同样调 `FUN_005A45E0`。挑格的取数点是内联的 `0x5A447D`
   （**不是** §2.5 那个外联 `0x598030`——同一段逻辑，一处内联一处不内联，
   属编译器的内联决策差异，不影响取数序列）。

雪地**没有** `work->+0x18`（碎石概率）的播种，也没有沙地/草地 LAT，
更没有石头 overlay——所以雪地地图的地表装饰明显比温带单调，这是原版行为。

## 四、`0x5A45E0` 树林补丁生长（3 取数点）

签名：`int FUN_005A45E0(CellClass* seed, int size, double prob)`，**返回真正栽下的树数**
（正是 §2.5 里从 `budget` 里扣掉的量）。

结构与陆地涂块 `0x59BBC0`、矿田生长 `0x5A28C0` **同一个模子**——
一次 Dijkstra 式最小堆扩散，优先级 = 到种子格的近似距离 + `[0,5)` 抖动：

```c
maxNodes = size * 25;
pool = new Node[maxNodes];              // new(size*200)，Node = { int32 xy; float key; }
heap = new MinHeap(maxNodes);           // new(0x14) + new(size*100+4)，1 基指针数组
pool[0] = { seed->xy, 0.0f };  used = 1;
Work[seed].+0x47 = 1;                   // 访问标记
heap.push(&pool[0]);
node = heap.pop();
planted = 0;
while (planted < size) {
    if (node == NULL || used >= maxNodes) break;
    cell = TryGetCellAt(&node->xy);
    if (IsPlain(cell) && cell->+0xE4 == 0 && cell->+0x44 == -1 && cell->+0xEC != 3) {
        // 取数点 0x5A47F2：r*(1/(2^32-1)) < prob 才栽
        if ((double)(uint32)Random() * (1/(2^32-1)) < prob) {
            // 取数点 0x5A481C：PickRange(1, 25)
            do { idx = _ftol((double)(uint32)Random() * (25/(2^32-1)) + 1.0); }
            while ((uint32)idx > 25);
            sprintf(buf, "TREE%d%d", idx/10, idx%10);      // 0x82C09C = "TREE%d%d"
            t = new TerrainClass(0xE0);
            if (t) TerrainClass::TerrainClass(t, TerrainTypes[FindIndex(buf)], &cell->xy);
            planted++;
        }
    }
    for (d = 0; d < 8; d++) {                              // 邻接表 [0x89F688]
        n = node->xy + Adj[d];
        if (!InUsableDiamond(n)) continue;
        if (Work[n].+0x47 != 0) continue;                   // 已访问
        if (Work[n].+0x45 != 0) continue;                   // 已锁定
        if (used >= maxNodes) continue;
        pool[used].xy  = n;
        // 取数点 0x5A49B8
        pool[used].key = (float)( FastSqrt((double)((sx-nx)² + (sy-ny)²))
                                  + (double)(uint32)Random() * 5.0 * (1/(2^32-1)) );
        Work[n].+0x47 = 1;
        heap.push(&pool[used]);  used++;
    }
    node = heap.pop();
}
return planted;
```

三个取数点的精度要点：

| 取数点 | 表达式 | 常量 | 备注 |
|---|---|---|---|
| `0x5A47F2` | `r * (1/(2^32-1))` 与 `prob` 比 | `[0x7ED898]` | `fcomp` + `test ah,1`：**`r < prob` 才栽**，失败也烧随机数 |
| `0x5A481C` | `_ftol(r * 25/(2^32-1) + 1.0)`，拒绝 `> 25` | `[0x7EDAD8]=25/(2^32-1)`、`[0x7E1718]=1.0` | **下界是 1，不是 0** → 只会生成 `TREE01`..`TREE25`，**`TREE00` 永远不出现** |
| `0x5A49B8` | `fild r; fmul 5.0; fmul 1/(2^32-1); fadd sqrt; fstp dword` | `[0x7ED800]=5.0`、`[0x7ED898]` | 全程 x87 双精度，**最后一步 `fstp dword` 才截成 `float`**；堆比较也用 `float` |

四条容易踩的细节：

1. **距离用的是"到补丁种子格"的距离**，不是到当前弹出格的距离
   （`0x5A4E42` 附近同款代码：`d² = (sx-nx)² + (sy-ny)²`，`sx/sy` 取自 `param_1->+0x24`）。
2. `Work[].+0x47` 访问标记在函数返回后**没有被清**——所以第 2 个补丁踩不进第 1 个补丁走过的格子，
   100 轮补丁是**互相排斥**的。它是否在阶段边界被清零，**未确认**。
3. 循环条件 `planted < size` 与 `used >= maxNodes`（`size*25`）两个都可能提前跳出，
   实际栽的树往往远少于 `size`。
4. `new` 失败时原版把指针置 0 就继续跑，随后照样解引用——**内存不足会直接崩**，
   照抄时不必模拟（复刻程序不会 OOM）。

## 五、`0x5A4B60` LAT 块写入器（1 取数点）

签名：`void FUN_005A4B60(CellClass* seed, int tileIndex, int count, int fieldId, char allowNonPlain)`，
§二/§三里三种 LAT 都靠它落地。**同一个最小堆扩散模板**，但这次是"铺满 `count` 格"：

```c
pool = new Node[count*?];               // new(count*0x50)
heap = new MinHeap(count*10);           // new(count*0x28+4)
pool[0] = { seed->xy, 0.0f };  used = 1;
Work[seed].+0x3C = fieldId;             // 注意：用 +0x3C（田块 id），不是 +0x47
heap.push(&pool[0]);
node = heap.pop();
for (i = 0; i < count && node; i++) {
    cell = TryGetCellAt(&node->xy);
    cell->+0x38 = tileIndex;            // ← 真正的地形写入
    for (d = 0; d < 8; d++) {
        n = CellStruct_Add(node->xy, Adj[d]);          // 0x42D510
        if (!InUsableDiamond(n)) continue;
        nc = TryGetCellAt(&n);
        bool ok = IsPlain(nc)
               || (allowNonPlain && (FUN_00486670(nc) || FUN_00486650(nc)));
        if (!ok) continue;
        if (Work[n].+0x3C == fieldId) continue;         // 本田块已收
        if (nc->+0x11C != 0 || nc->+0x44 != -1 || nc->+0xE4 != 0) continue;
        pool[used].xy  = n;
        // 取数点 0x5A4E6E
        pool[used].key = (float)( FastSqrt((double)((sx-nx)² + (sy-ny)²))
                                  + (double)(uint32)Random() * 5.0 * (1/(2^32-1)) );
        Work[n].+0x3C = fieldId;
        heap.push(&pool[used]);  used++;
    }
    node = heap.pop();                                  // 0x5AC960
}
```

- 阶段 10 的三个调用点 **`allowNonPlain` 全传 0**，所以 `0x486670` / `0x486650`
  两个谓词在本阶段是死分支。它们的语义已在阶段 4 架桥那边定死：
  `0x486650` = `TileIndex ∈ [MiscPaveTile(0xAA10A4), +0x0E)`、
  `0x486670` = `∈ [PaveTile(0xABC2B0), +0x10)`，都是"铺装地面"判据
  （见 [stage-making-regions.md](stage-making-regions.md) §五之三）。
- 用 `+0x3C` 当"已收"标记，正好对上 §2.1 那次全图清零——
  **每次调用换一个 `fieldId`（= `cell.Y*512 + cell.X`），所以不需要清**。
- 抖动常量是 `[0x7ED808] = 5.0`（与 `0x5A45E0` 的 `[0x7ED800]` 是**两份独立的 5.0**，
  数值相同，MSVC 没有合并；这不影响结果，但说明这两段代码是分别写的）。

| 函数 | 取数点 | 状态 |
|---|---|---|
| `0x5A45E0` | `0x5A47F2` `0x5A481C` `0x5A49B8` | ✅ 已实锤 |
| `0x5A4B60` | `0x5A4E6E` | ✅ 已实锤 |
| `0x598000` `UniformUnit` | `0x598003` | ✅ 已实锤 |
| `0x598030` `PickRange` | `0x59805E` | ✅ 已实锤 |

## 六、取数账本

`memory/data/rmg/rng_ledger.txt` 记的 18 处：

```
598003 59805E 5A3B58 5A3B88 5A3BB8 5A3C53 5A3D3F 5A3E1B 5A40F0 5A4157
5A41DE 5A4221 5A434D 5A447D 5A47F2 5A481C 5A49B8 5A4E6E
```

归属：

| 取数点 | 归属 | 语义 |
|---|---|---|
| `5A3B58` `5A3B88` `5A3BB8` | `0x5A3AE0` | 三个 `PickRange(20,40)` 中心尺寸 |
| `5A3C53` `5A3D3F` `5A3E1B` | `0x5A3AE0` | 逐格三次抛硬币（碎石/沙/草） |
| `5A40F0` | `0x5A3AE0` | `PickBelow(n/200+1)` 石头数量 |
| `5A4157` | `0x5A3AE0` | `PickBelow(side²)` 石头选格 |
| `5A41DE` `5A4221` | `0x5A3AE0` | `PickBelow(5)` overlay 变体 |
| `5A434D` | `0x5A4280` | 雪地逐格抛硬币 |
| `5A447D` | `0x5A4280` | 树林补丁内联选格 |
| `59805E` | `0x598030` | 树林补丁选格（温带走外联） |
| `598003` | `0x598000` | 所有 `TruncNormal` 的底层均匀源 |
| `5A47F2` | `0x5A45E0` | 栗树抛硬币（vs `prob`） |
| `5A481C` | `0x5A45E0` | `PickRange(1,25)` 选树种 `TREE01`..`TREE25` |
| `5A49B8` | `0x5A45E0` | 最小堆优先级抖动 `[0,5)` |
| `5A4E6E` | `0x5A4B60` | 最小堆优先级抖动 `[0,5)` |

**注意 `NormalDeviate` 的缓存跨阶段串**：本阶段第一次 `TruncNormal` 可能一个随机数都不抽
（直接取阶段 9 剩下的那半个），详见 [fast-math.md](fast-math.md)。

## 七、未确认清单

1. ✅ 已关：`0x5A4B60` 的第三个参数是**格数**（`for (i=0; i<count; i++)` 每轮写一格）；
   但 pool 容量 `new(count*0x50)` 与堆容量 `count*10` 为何比 `count` 大这么多——**未确认**
   （猜是"预留待展开的邻居"，但 `0x50` 这个数字与 Node 尺寸 8 字节对不上）。
2. ✅ 已关：`0x5A45E0` 返回**真正栽下的树数**（`local_54`）。
3. overlay 序号 `0xA8..0xAC` / `0xAD..0xB1` 对应的具体美术资源名——**未确认**，
   只实锤了"两组各 5 个变体，按格子是否沙地 LAT 二选一"。
4. `0x42B1F0` 里 `[0x87F8DC]` / `[0x87F8E0]` 的确切字段名（推测是 `MapRect.Width/Height`，
   偏移 `MapClass+0xF4/+0xF8`）——**未确认**。
5. `cell->+0xE4` 的字段名——**未确认**。`cell->+0x11E` 已确认是 `OverlayData`：
   阶段 4 架桥用它存桥面的行号/列号（0..2），正是 overlay 子帧号
   （[stage-making-regions.md](stage-making-regions.md) §五之三）。
6. 雪地分派里内联铺的 `+0x28 = 0.005` / `+0x20 = 0.001`，为什么不铺 `+0x18`——
   `work->+0x18` 在雪地路径上是**上一阶段的残值**还是恒 0，**未确认**，
   但它在 `0x5A4280` 里根本没被读，所以不影响本阶段输出。
7. `Work[].+0x47`（访问标记）在何处被清零——**未确认**。
   `0x5A45E0` 自己不清，所以 100 轮树林补丁互相排斥；
   这是有意设计还是泄漏，得把阶段 1 的初始化跟完才能定。
8. ~~`0x486670` / `0x486650` 两个谓词的语义未确认~~ —— **已确认**为 `PaveTile` /
   `MiscPaveTile` 的砖号区间判据（[stage-making-regions.md](stage-making-regions.md) §五之三）；
   在阶段 10 里仍是死分支。
9. `TruncNormal` 的接受区间是闭区间（`v < lo || v > hi` 才重抽），
   与阶段 9 一致，已在本阶段两处独立复核。

## 八、RA1 源码这次帮不上忙（边界说明）

`CnCRemastered` 的 RA1 源码是 RA2 的**参考答案**——随机数发生器、
`.ini` 读取、寻路这些"引擎底座"都能直接对照（见
[cncremastered-source.md](../references/cncremastered-source.md)）。
但本阶段对不上：

- RA1 **没有随机地图生成器**。全树搜 `MapSeed` / `Ruggedness` / `random map`
  零命中，RMG 是 Tiberian Sun 时代才有的东西，而 TS 源码未公开。
- RA1 是 2D 正方格 + `TemplateTypeClass`，**没有 LAT 自动过渡**这套机制；
  `CDATA.CPP` 里那张巨大的 `TEMPLATE_*` 表是手工枚举，和
  `CellClass::SetupLAT` 的邻接推导不是一回事。

所以阶段 10 只能靠反汇编硬啃。**记下这条边界本身也是结论**：
凡涉及"等距地图 / LAT / 高度场 / RMG"的部分，RA1 源码无参考价值；
凡涉及"随机数 / ini / 单位逻辑 / 存档"的部分，优先去查 RA1。

## 九、复刻状态：已落 C++ 并逐位对拍

代码 `code/rewrite/rmg/lats.{h,cpp}` + `lat_spread.cpp`，
基准 `memory/data/rmg/lats_golden.txt`（生成脚本 `code/randmap/dump_lats_golden.py`），
用例 `code/rewrite/rmg/test/test_lats.cpp`，ctest 目标 `lats`。

### 落地清单

| 原版 | C++ | 对拍 |
|---|---|---|
| `0x59921F` 调度 | `CreateLats` | ✅ `temp*` / `snow*` 五例（整段跑机器码） |
| `0x5A38C0` 播概率图 | `SeedVegetationProbabilities` | ✅ `v-*` 四例 |
| `0x5A3AE0` 温带主体 | `CreateLatsTemperate` | ✅ 随 `temp*` |
| `0x5A4280` 雪地主体 | `CreateLatsSnow` | ✅ 随 `snow*` |
| `0x5A45E0` 树林补丁 | `GrowTreePatch` | ✅ `t-*` 七例 |
| `0x5A4B60` LAT 块写入器 | `WriteLatBlock` | ✅ `w-*` 七例 |
| `0x47CA80` `SetupLAT` | `LatWorld::SetupLAT` | 只对**调用次数**（要读 .tmp 美术资源） |
| `0x47D2B0` `RecalcAttributes` | `LatWorld::RecalcAttributes` | 只对 (格子, 参数) 序列 |
| `0x71BB90` `TerrainClass` | `LatWorld::CreateTerrain` | 只对 (名字, 落点) 序列 |

不打桩、真身跑起来的：`PickRange 0x598030`、`NormalDeviate 0x5980C0`、
`FastSqrt 0x4CAC40`、`GetCellAt 0x5657A0`、四个砖号谓词
（`0x486380` / `0x4865B0` / `0x486790` / `0x4867B0`）、
`0x42B1F0` 地图规模、最小堆的上浮/下沉/弹出、以及**全图遍历器
`0x578350` / `0x578290` 本身**。

### 25 个用例

| 用例 | 打的点 | 取数 |
|---|---|---|
| `w-plain` / `w-count1` / `w-big` | 常规扩散、`count=1`、大图 `count=40` | 41 / 8 / 98 |
| `w-blocked` | 一半格子非未定砖，扩散被切碎 | 29 |
| `w-obstacles` | `+0x11C` 坡道 / `+0x44` overlay / `+0xE4` 三条硬拦 | 18 |
| `w-queued` | `+0x3C == fieldId` 的格子不再入堆 | 21 |
| `w-pave` | **`allowNonPlain=1` 死分支**（合成的 `PaveTile`/`MiscPaveTile` 砖号） | 49 |
| `t-plain` / `t-low` / `t-tiny` / `t-big` | 栽树概率 0.5 / 0.03 / 0.9 / 0.35 | 134 / 368 / 15 / 269 |
| `t-flag45` / `t-flag47` | 两条不同的邻居拦截 | 80 / 78 |
| `t-blocked` | 栽树自身四条门槛（非未定砖 / `+0xE4` / overlay / `+0xEC==3`） | 219 |
| `v-plain` / `v-veg0` | 非岸格播种，`Vegetation=0` 边界 | 0 / 0 |
| `v-shore` | **5×5 那个原版 bug**（判定用邻居、写入用中心） | 0 |
| `v-taken` | `+0x28 != 0` 的格子不再播种 | 0 |
| `temp-a` / `temp-b` / `temp-veg0` | 温带整段，含预算为 0 的退化 | 1709 / 1671 / 600 |
| `temp-big` | 24×16，撒石头那段名额才挑得起来 | 4479 |
| `snow-a` / `snow-b` / `snow-big` | 雪地整段 | 1243 / 1236 / 2887 |

每例比七样：取数个数、返回值、`SetupLAT` 次数、`TREExx` 名字与落点、
`RecalcAttributes` 的 (格子, 参数)、以及**每一格**跑完之后的
`(砖号, overlay, +0x11E, +0x3C, +0x47, 三个概率槽)`——概率槽按**位**比，不留容差。

### 这一轮新实锤的三件事

1. **雪地内联铺常量那圈遍历的覆盖面 = 菱形本身。**
   `0x578350`/`0x578290` 是「起点 (1, W)、每步 (X+1, Y−1)、走完一条对角线后
   按 `(Y−W+X−1)` 的奇偶换行」的对角线遍历器，靠**格子指针为 NULL** 停机。
   实测它吐出的格子集合与 RMG 菱形判据
   （`X+Y > W && |X−Y| < W && X+Y <= W+2H`）逐格一致，
   所以 `MapGrid::ForEachCell` 那套复刻是对的——而且**顺序也必须一致**，
   温带主体是逐格取随机数的。
2. **`0x5A4B60` 的池子确实越不了界。**
   `used <= 8*count + 1`，池子 `count*10` 条，`count >= 4`（调用点 `TruncNormal`
   的下界），所以那条缺失的上界检查是**良性**的——不像阶段 8 `CreateOreField`
   同样的缺口是真 UB。25 个用例（含 `count=1` 的退化）没有一次触到边界。
3. **`0x486670` / `0x486650` 是两个独立谓词，短路顺序为前者在先。**
   `w-pave` 用两段合成砖号（`PaveTile=600..615`、`MiscPaveTile=700..713`）
   把这条死分支走通了，两个谓词各自命中的格子数都不为 0。
   复刻侧因此保留成 `LatWorld::IsPaveTile` / `IsMiscPaveTile` 两个虚函数，
   不合并成一个——合并会丢掉「前者为真时后者根本不被调用」这个事实。

### 没覆盖到的路

- `0x5A3AE0` 里 `tries > 200` 之后**强行用格 (0,0)** 那条：需要一张几乎全是
  已定砖的图才逼得出来，25 个用例都没触发（代码照抄了，但没有基准背书）。
- 两处 `TruncNormal` 的回退分支（`mu ∈ [20,40]`、`sigma = 20` 使其不可达），
  以及 `operator new` 返回 0 的那几条——原版同样不可达。
