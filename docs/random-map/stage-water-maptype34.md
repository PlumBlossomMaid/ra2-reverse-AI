# 阶段 2 支路：`MapType ∈ {3,4}` 的河流与湖泊

> 入口 `0x59C580`（`__fastcall`，`ecx = MapSeedClass*`），由阶段 2 调度器
> `0x59A6C0` 在铺完陆地之后调用。
> 取证：`memory/data/decomp/rmg_maptype34.txt`（`0x59D510` / `0x59C920` /
> `0x5A0160` / `0x5A0410` / `0x5A0700` / `0x59C580`）与
> `rmg_maptype34b.txt`（`0x59E740` / `0x5A08D0`）。

`MapType` 0/1/2 那三种投放策略产出的是「陆地 + 环绕的海」，
本支路负责在那之上再挖一条**河**和若干**湖**。它只在
`MapType == 3 || MapType == 4` 且 `WaterAmount > 20` 时才走完整流程；
否则只做后半段的湖。

## 一、`0x59C580`：调度

```c
void MapSeedClass::AddRiversAndLakes()      // 0x59C580
{
    this->blobId += 1;                                   // +0x308
    if ((MapType == 3 || MapType == 4) && WaterAmount > 0x14) {   // +0x3C / +0x4C
        char ok = 0;
        for (int i = 0; i < 10 && !ok; ++i) {
            CellStruct start = {0, 0};
            ok = River(&start, 0.0, 0);                   // 0x59D510
        }
        if (ok) this->blobId += 1;      // 十次全失败就**不加**，编号不推进
    }
    char ok = 0;
    for (int i = 0; i < 10 && !ok; ++i) {
        CellStruct start = {0, 0};
        ok = WaterBlob(&start);                          // 0x59C920
    }
    if (ok) this->blobId += 1;
}
```

两处细节值得单记：

* **`WaterAmount > 20`**，不是「非零」。水量滑条给不到 20 以上时，
  这条支路等于被关掉一半，地图上只剩湖没有河。
* 河与湖各自**最多重试 10 次**，且 `blobId`（`+0x308`）只在成功时 +1。
  失败不消耗编号，但**消耗随机数**——每次重试都会把起点、角度、河宽、
  桥阈值重新抽一遍，这是 RNG 总账里必须逐次记的。
  起点 `{0,0}` 是「你自己随便选一个」的哨兵；非零则是调用方指定
  （河口挖湖走的就是这条）。

## 二、`0x59C920`：往陆地里涂水（湖）

`__thiscall(this; CellStruct* start)`，`ret 4`，返回 `char`。
它是阶段 2 主涂块器 `0x59BBC0` 的**镜像**：那个往海里涂陆地，这个往陆地里涂水。

### 预算

```
budget = (WaterAmount != 0)
       ? _ftol( (double)side_h * side_w * WaterAmount * 0.008 + 100.0 )   // 0x59C942-0x59C95E
       : 0;
budget -= this->paintedTotal;        // +0x304，先前几块水已经花掉的格数
if (budget <= 0x4B) return false;    // 0x59C969
```

`+0x184`/`+0x180` 用 `fild`/`fimul` 做整数乘（面积），常数
`[0x7ED9F8] = 0.008`、`[0x7E2AC0] = 100.0`。
注意 `WaterAmount == 0` 时 `eax` 保持为 0（`mov eax,[ebx+0x4c]` 的残值），
于是 `budget = -paintedTotal`，必然 `<= 0x4B` 而直接失败——这条支路走不通，
但它是**靠寄存器残值**成立的，复刻时得照写。

### 「禁区」预处理（只在 `start == (0,0)` 时）

1. 扫全图，把每格工作记录的 `+0x3C`（入堆标记）清零；
2. 把整个工作数组的 `+0x44` 清零；
3. `Erode(0, 2, -2)`（`0x5A0410`，见 §三）——把**未归属区域**（owner == 0）
   往内剥两圈，剥掉的格子标成 `-2`；
4. 再扫一遍：`owner == 0 || owner == blobId` 的格子置 `+0x44 = 1`（允许）；
   `owner == -2` 的格子把 `+0x38` 恢复成 0，但 `+0x44` **不置**（不允许）。

净效果：**在每一块已归属区域外侧留出 2 格宽的禁涂带**。
湖不许贴着已有的陆块/水块生长，中间必须隔两格。

`start != (0,0)`（河口挖湖）时跳过整套，全图 `+0x44 = 1`。

### 随机起点

```c
do {
    do { rx = ...; } while (rx > mapWidth - 1);      // [0x87F8DC] - 1，拒绝采样
    do { ry = ...; } while (ry > mapHeight - 1);     // [0x87F8E0] - 1
    X = rx + ry + 1;
    Y = ry - rx + mapWidth;                          // 与 0x59BBC0 同一套 iso 变换
    if (++attempts >= 200) return false;             // 先加先判，再验合法性
} while (!(work[Y][X].owner == 0 && IsUndecidedTile(cell) && work[Y][X].allow != 0));
```

`if (199 < attempts)` 那一句排在合法性判断**之前**：第 200 次抽到的坐标即使合法
也会被丢掉。原样保留。

### 这一块要涂多大

```c
clamped = max(budget, 0x4C);
mean  = budget / 3;          // 整数除法，先除后转 double
sigma = budget / 6;
if ((mean - sigma) > clamped || (mean + sigma) < 75.0) {
    sigma = (clamped - 75.0) * 0.5;
    mean  = sigma + 75.0;
}
do { v = Normal() * sigma + mean; } while (v < 75.0 || v > clamped);
thisBudget = _ftol(v);
```

`[0x7ED9F0] = 75.0`、`[0x7E1738] = 0.5`。截尾正态的这套「区间兜不住就把
均值/标准差重算成区间中点/半宽」的写法，与河流的角度、宽度抽样是同一个模板
（RA1 `RANDOM.CPP` 的「Keep picking random numbers until it fits」同宗）。

### 主循环

最小堆，弹出优先级最小者，涂成水：

```c
work[c].owner = blobId;
cell->IsoTile = [0xAA0738];        // WaterSet 基址
cell->SubTile = 0;
```

再看 4 个正交邻居（`d ∈ {0,2,4,6}`，**不是** 8 邻域）：

| 拒绝条件 | 是否把整块判失败 |
| --- | --- |
| `work.owner != 0` | 是（`ok = false`）——撞上别人的地盘 |
| `work.+0x3C == blobId` | 否，只是已入过堆 |
| `!IsUndecidedTile(cell)` | 否 |
| `work.+0x44 == 0` | 否，禁涂带 |

接受则计算优先级：

```c
d2   = (start.X - c.X)^2 + (start.Y - c.Y)^2;
dist = (float)FastSqrt(d2);                                  // 0x4CAC40 查表开方
u    = (float)((uint32)Random() * 2.3283064370807974e-10);   // [0x7ED898] = 2^-32
prio = (float)(10.0f * u + 0.5f * dist - 0.02f * ring);
```

常数：`[0x7ED768] = 10.0f`、`[0x7ED764] = 0.5f`、
`[0x7ED76C] = 0.019999999552965164f`（即 `0.02f`）。
和陆地涂块的「引力中心随机游走」不同，湖的优先级里**没有正态偏差**，
只有一个均匀随机项 + 到起点的距离 + 一个随圈数递减的项。
`-0.02*ring` 让后涂的格子优先级更低（更早被弹出），效果是圆盘状铺开而非枝杈状。

### 成败与收尾

```c
success = ok && ring > 0x4B && ring > thisBudget / 4;    // 有符号除 4：cdq/and 3/add/sar 2
```

成功且 `start == (0,0)` 时还要：

1. `Shoreline(0x57A0C0)(blobId, flag = 0)` —— 四遍岸线平滑；
2. `Dilate(blobId, 1, {0,0,0x200,0x200}, 0, 0)`（`0x5A0160`，见 §三）
   —— 既是「这块水有没有和第三方区域贴上」的**隔离检测**，也顺手把外围一圈收归己有；
3. 把所有 `work.owner == blobId` 且砖号仍是 `0`/`0xFFFF` 的格子刷成
   `[0xAA0E18]`（绿地砖）。

任何一步失败就整块回滚：

```c
work.owner = 0;  work.+0x4B = 0;
cell->IsoTile = 0;  cell->SubTile = 0;  cell->Level = this->level;   // +0x30C
```

## 三、三个区域算子

三者都建立在 `0x5A0700` 之上，是本支路（以及阶段 4「Making regions」）的公共积木。

### `0x5A0700 CollectBorderCells(this; owner)`

返回一个 `DynamicVector<packedCoord>*`（vtable `0x7E3890`），装的是
**所有 `work.owner == owner`、且 8 邻域里存在一个界内邻居其 owner 不同**的格子。

* 出界的邻居**不算**「不同」——贴着地图边缘的格子不会因此被当成边界。
* 容量增长步长 `vec->+0x14 = this->+0x184 * this->+0x180`（地图面积）。
* 另外两个调用点：`0x58CAD7`（Init regions）与 `0x5A0A58`（在 `0x5A08D0` 里）。

### `0x5A0410 Erode(this; owner, rings, newOwner)` — `ret 0xC`

把编号为 `owner` 的连通块**向内剥 `rings` 圈**。每剥掉一格：

```c
work.owner    = newOwner;
cell->IsoTile = 0;
cell->SubTile = 0;
cell->Level   = this->level;      // +0x30C
```

`work.+0x3C` 被借用作「本圈已访问」的戳，值是 `ring + 1`；`rings > 1` 时
在开头和结尾各清一次。**恒返回 true**。

### `0x5A0160 Dilate(this; owner, rings, rectX, rectY, rectW, rectH, flag, cellVal)` — `ret 0x20`

把 `owner` 向外长 `rings` 圈，限制在给定矩形内。接受一个邻居的条件：

```c
   (work.owner == 0 && IsUndecidedTile(cell))
|| (flag != 0 && work.owner == owner - 1 && IsWaterOrShore(cell))
```

接受则 `work.owner = owner`；若 `flag != 0` 且该格未定砖，还写
`cell->Level = cellVal`。

**关键出口**：只要遇到一个 owner 非 0、又不满足接受条件、且 `owner != 本块`
的邻居，函数**立刻返回 false**。所以调用方用它做「我这块有没有和别人贴上」的
隔离测试。下一圈的容量步长是 `newVec->+0x14 = cur->count * 3`。

## 四、`0x59D510`：河流生成器

```c
char __thiscall River(this; CellStruct* start, double angle, char isBranch);   // ret 0x10
```

**递归**——它会以约 1% 的概率给自己生一条支流。

### 起点与初始角度（仅 `start == (0,0)` 时抽）

```c
dir = PickBelow(4);          // Random() * 9.31322574832319e-10，> 3 就重抽
rx  = PickBelow(mapWidth);
ry  = PickBelow(mapHeight);

M  = 7π/4 - dir * π/2;       // ∈ {π/4, 3π/4, 5π/4, 7π/4}——从某条边斜指向图心
HI = M + π/4;   LO = M - π/4;
sigma = 0.5235987755982988;  // = π/6，[0x7EDA28]
mean  = M;
if (HI < M - π/6 || M + π/6 < LO) {          // 恒不成立，死分支
    sigma = (HI - LO) * 0.5;  mean = sigma + LO;
}
do { angle = Normal() * sigma + mean; } while (angle < LO || angle > HI);
```

那个 `if` 的两个条件分别化简为 `π/4 < -π/6` 与 `π/6 < -π/4`，永远为假。
它和 `0x59C920` 的块尺寸、下面的河宽抽样共用同一个代码模板，模板在这里
退化成了死分支——按「bug 也要复刻」的原则原样保留。

四个候选起点预算在栈数组 `[esp+0xD0..0xDC]`，按 `dir` 取一个：

| dir | 起点 | 说明 |
| --- | --- | --- |
| 0 | `(rx + 1, mapWidth - rx)` | = iso(rx, 0)，上边 |
| 1 | `(W + H - 1 - ry, H - ry)` | 右边（iso 恒等式**未确认**） |
| 2 | `(W + H - 1 - rx, H + rx)` | 下边（iso 恒等式**未确认**） |
| 3 | `(ry + 1, ry + mapWidth)` | = iso(0, ry)，左边 |

`start != (0,0)` 时直接用传入的起点与角度（支流走这条）。
接着 `if (!InDiamond(start)) return false`。

### 河宽与桥阈值

```c
wmax  = _ftol( max(1.0, WaterAmount * 0.07) );      // [0x7EDA20]=0.07, [0x7E1718]=1.0
width = PickRange(1, wmax);                          // 内联：_ftol(u*wmax + 1.0)，> wmax 重抽
half  = width / 2;                                   // 截断
widthLo = width - half;   widthHi = width + half;

bridgeThreshold = PickRange(35, 125);                // _ftol(u * 91*2^-32 * ... + 35.0)
                                                     // [0x7EDA18]=91*2^-32, [0x7EDA10]=35.0，>125 重抽
```

### 逐步推进

每一步：

```c
c = Cos(angle);      // 0x4CAD00，查表，见 §五
s = Sin(angle);      // 0x4CACB0
n = _ftol(curWidth); // 本步横截面有几格
// 横截面两端：x ∓ (n-1)*s*0.5 , y ∓ (n-1)*c*0.5   —— 垂直于行进方向
for (每一格) {
    if (界外) continue;
    if (work.owner == 0 && IsUndecidedTile(cell)) {
        work.owner    = blobId;
        cell->IsoTile = [0xAA0738];   // 水
        cell->SubTile = 0;
    } else if (work.owner 是别人) ok = false;
}
// 两个「本次横截面 X（或 Y）是否恒定」的标志
if (|c| <= |s|) flagA = 0; else flagB = 0;
x += c;
y -= s;              // 屏幕 Y 向下，所以是减
```

`x += cos` 配 `y -= sin` 这一对，是我判定 `0x4CAD00` 是余弦、`0x4CACB0`
是正弦的第二条证据（第一条见 §五）。

### 架桥

```c
if (!isBranch && (flagA || flagB) && bridgeCount < 1
    && |...| < π/4                       // [0x7E3D88] = 0.7853981633974483
    && this->+0x310 != 0                 // 就是它，见下
    && steps > bridgeThreshold)
{
    dir = flagA ? (c <= 0 ? 6 : 2) : (s <= 0 ? 4 : 0);
    PlaceBridge(blobId, &coordA, &coordB, dir, &okOut, &x, &y);    // 0x59E740
    if (okOut) { bridgeCount++; this->+0x308 += 1; }
}
```

**`MapSeedClass +0x310` 的用途至此实锤**：它是 Init random map（`0x59A4C7`）
里那个 25% 的抛硬币结果，唯一的读取点在这里——**它决定这张图的河上有没有桥**。
（此前文档里只记到「唯一读者 `0x59DCFC`」，那正是 `0x59D510` 的内部地址。）

### 支流

```c
if (u < 0.01 && ok && !isBranch && bridgeCount == 0) {       // [0x7E3808] = 0.01
    // 截尾正态：mean = angle + π/2, sigma = π/6, 区间 [angle + π/6, angle + 5π/6]
    //           [0x7EDA08] = 2.6179938779914944 = 5π/6
    isBranch = 1;
    River(&here, branchAngle, 1);        // 递归
}
```

支流**不会**再生支流（`isBranch` 已置 1），也不会架桥。

### 角度与宽度游走

```c
if (steps > 5) {
    // 截尾正态，mean = 0，sigma = 0.3141592653589793 (= π/10)
    // 区间 [-π/10, +π/10]：[0x7EDA00] = -0.3141592653589793、[0x7E8AF0] = +0.3141592653589793
    angle += delta;
}
if (half > 0) {
    // 截尾正态，mean = 0.0，sigma = 0.5，区间 [widthLo - curWidth, widthHi - curWidth]
    // [0x7E3CB8] = -0.5、[0x7E1738] = 0.5
    curWidth += delta;
}
```

头 5 步河道是**直的**，之后才开始摆动。

### 何时停

```c
    steps++;
    u = (uint32)Random() * 2^-32;
} while (u >= 0.005);          // [0x7E44E8] = 0.005
```

每步 0.5% 的停止概率，期望长度 200 步。停下之后：

```c
if (steps < 0x28) ok = false;              // 少于 40 步算失败
// 河口：两次 _ftol 得到一个坐标
if (InDiamond(mouth) && ok) {
    if (!WaterBlob(&mouth)) ok = false;    // 0x59C920 —— 在河口挖一个湖
}
```

### 收尾

```c
if (!isBranch && ok) {
    Shoreline(blobId, 0);                                   // 0x57A0C0
    Dilate(blobId, 1, {0,0,0x200,0x200}, 0, 0);             // 隔离检测 + 外扩一圈
    // work.owner == blobId 且砖号仍是 0/0xFFFF 的，刷成 [0xAA0E18] 绿地
}

if (bridgeCount == 0 && ok && this->level == 4) {           // +0x30C
    if (!isBranch) u = 抽一次;
    if (u < 0.7) {                                          // [0x7ED778] = 0.7
        if (AngularGrow(blobId, 0.01f, {0,0,0x200,0x200}, &start, 1)) {   // 0x5A08D0
            Dilate(blobId, 6, {0,0,0x200,0x200}, 0, 0);
            // 所有 work.owner != blobId 的格子 cell->Level += 4
            // 并置一个标志，稍后 this->+0x30C += 4
        }
    }
} else {
    Dilate(blobId, 2, {0,0,0x200,0x200}, 1, this->level);
}

this->paintedTotal += steps;        // +0x304
return true;
```

**没架桥的河会被下切成峡谷**：`+0x30C == 4`（当前地形层高为 4）时，
有 70% 的概率把河两岸整体抬高 4 层，河自己留在低处。
架了桥的河就不下切——桥要横跨的是平地上的河，不是峡谷。

失败则回滚所有 `work.owner == blobId` 的格子；若已经架过桥，
连 `blobId - 1` 的格子一起回滚（桥自己占了一个编号）。

## 五、查表 sin/cos：`0x4CACB0` / `0x4CAD00`

河流是全 RMG 里**唯一**用到三角函数的地方。原版没有调 `sin`/`cos`，
而是查一张写死在 `.rdata` 里的 `float[0x2800] @ 0x84F084`。

```
0x4CACB0  sin:
  fld  qword [esp+4]
  fmul dword [0x8223B0]          ; 2607.594482421875f = chop_f(8192/π)
  call 0x7C5F00                  ; _ftol，向零截断
  mov  cl, al                    ; 保存奇偶位
  cdq / sub eax,edx / sar eax,1  ; 向零截断的 /2
  and  eax, 0x80001FFF           ; ┐
  <13 位符号扩展再折回>          ; ┘ 逐条走完等价于 eax &= 0x1FFF
  test cl,cl / cmp eax,0x1FFF    ; 奇数且没到顶就 +1（等价于四舍五入）
  fld  dword [0x84F084 + eax*4]

0x4CAD00  cos: 完全一样，只是最后 +0x800（四分之一周期），上限改成 0x27FF
```

`0x8223B0`（正弦用）与 `0x8223B4`（余弦用）是两个槽位、同一个 float 值：
`8192/π` 截断到 float 得 `2607.594482421875`（真值 `2607.5945876176133`）。

### 表是什么

逐项比对的结论：**`table[i] == chop_to_float(sin(i · 2π / 8192))`，
10240 项里 10238 项逐位相符**。两个例外：

| i | 表里的值 | 数学真值 |
| --- | --- | --- |
| 4096 | `1.2246063538223773e-16` | `sin π = 0` |
| 8192 | `-2.4492127076447545e-16` | `sin 2π = 0` |

这两个数正是 x87 `fsin` 的经典现象：`fsin` 内部只用 66 位的 π 做归约，
`fsin(M_PI)` 返回 `1.2246063538223773e-16`，而正确舍入的
`sin(nearest_double(π))` 是 `1.2246467991473532e-16`。
也就是说**这张表是当年用 x87 `fsin` 离线生成的**，两处 ~1e-16 的噪声
是那次生成留下的指纹。我们照原样保留。

排除过的其他生成式（`i*(2π/8192)`、`(i*2π)/8192`、`2π*(i/8192)`、
`(i/8192)*2π`、15 位 π 字面量）都给出同样的这 2 项偏差，
说明差异出在**正弦求值**而非参数构造。

表有 `0x2800 = 10240` 项而一个周期只要 8192 项：余弦靠 `+0x800` 的相位偏移
查同一张表，把下标顶到 `0x27FF`，多出来的 2048 项就是给它留的。

### 必须照搬的三点

1. 角度被量化到 **1/8192 圈 ≈ 0.0439°**，返回值只有 float 精度；
2. `_ftol` 与折半**都是向零截断**，于是 `f(-x) != -f(x)`——
   正负两侧落在**相邻的两个表项**上。河流每步 `x += Cos(θ)`、`y -= Sin(θ)`，
   这份不对称会实打实改变河道走向；
3. `sin π` / `sin 2π` 那两项不是 0。

### 复刻状态 ✅

* 表：`code/rewrite/rmg/sin_table.h`（10240 项，存 float 位形），
  由 `code/randmap/gen_sin_table.py` 从 exe 导出并当场自检。
  写死而不运行期生成的理由同 `sqrt_table.h`——CI 可能跑在 x86 / ARM / 龙芯，
  写死才能让「结果与架构无关」在编译期就成立。
* 函数：`ra2::rmg::Sin` / `Cos`（`fast_math.{h,cpp}`），逐条对着汇编写，
  连那段「等价于 `& 0x1FFF`」的无效符号扩展也照抄了——删掉它才需要举证。
* 对拍：
  * `Trig.MatchesOriginalLookupTable`：82 组，基准由
    `code/randmap/dump_sincos_golden.py` 用 Unicorn 跑原版机器码取得，
    存档 `memory/data/rmg/sincos_golden.inc`。这两个函数只有一次 `fmul`
    加整数运算，不碰 `fyl2x`/`fsin`，所以 Unicorn 在此可信；
  * `Trig.NegativeArgumentIsNotMirrored`：钉死上面第 2 点；
  * `Trig.TableIsPureMath`：数值层允许 1 个 float ulp（`std::sin` 的最后 1 bit
    是逐平台的），结构层的反对称 / 关于 π/2 对称 / 周期 8192 三条**逐位精确**，
    两处 `fsin` 残渣单独钉死。

> 顺带记一笔：`0x4CAD50` 起还有一个用 `[0x7E8970] = 651.8986469044033`
> （≈ 2048/π）的同族函数，查的是同一张表但步长是 1/2048 圈。RMG 不走它。

## 六、`0x59E740`：架桥（提纲）

`__thiscall(this; blobId, CellStruct* a, CellStruct* b, int orientation,
char* okOut, double* x, double* y)`。

`orientation ∈ {0, 2, 4, 6}` 四个朝向，函数体是一个大 switch，
按朝向拼出矩形与偏移表，再去 `BridgeSet` 里取砖。
调用方靠 `okOut` 判断桥有没有真的落地。

**TODO**：整函数尚未逐条拆开（那些偏移表还没导出）。
它内部没有取数点，不影响随机数流，所以可以晚于河流主体复刻。

## 七、`0x5A08D0`：角度优先的块扩张（提纲）

`char __thiscall(this; blobId, float weight, Rect* r, CellStruct* origin, char flush)`。

* 分配 `nodes[max(2 * W * H, 100)]` 与一个最小堆；
* 基准角由起点贴着矩形的哪条边决定（`0`、`π/2`、`π`、`3π/2`）；
* 种子来自 `CollectBorderCells(blobId)`，按矩形过滤；
* 优先级 `(float)(2.0 * u + 1.5 * angleDev)`，两个系数是 `.rdata` 里的
  **double**：`[0x7ED790] = 2.0`、`[0x7ED798] = 1.5`
  （一开始按 float 读成了 `0.0`——消费指令是 `fmul qword ptr`，读宽度必须跟着走）；
* `angleDev` 用 `FUN_004CADE0`（atan）折进 `[0, π]`：反复 `-2π`，若仍 `> π` 则取 `2π - v`；
* 迭代次数 `n = _ftol(...)`，再抽 `n2 = PickBelow(n/2 + 1)`，共循环 `n + n2` 次；
* 每次弹堆、认领（`work.owner = blobId`、`+0x4B = 1`），把 8 邻域中
  未归属、未定砖、在矩形内、且 `work.+0x3C != blobId` 的推进堆；
* 每轮用 `Normal() * π/4` 微调基准角；撞上第三方 owner 则 `ok = false`；
* `flush != 0` 时最后把堆里剩下的仍未归属的格子全部认领。

**TODO**：`weight` 参数（河流传的是 `0x3C23D70A` = `0.01f`）在优先级公式里
的确切位置、`FUN_004CADE0` 是不是 atan 的最终确认、
两个堆辅助 `FUN_005AD870` / `FUN_005AC340` 的细节。

## 八、复刻状态与未确认清单

| 件 | 状态 |
| --- | --- |
| `0x59C580` 调度 | ✅ 已实锤，未落 C++ |
| `0x59C920` 湖 | ✅ 已实锤（预算/禁区/起点/正态/优先级/回滚），未落 C++ |
| `0x5A0700` / `0x5A0410` / `0x5A0160` | ✅ 已实锤，未落 C++ |
| `0x59D510` 河 | ✅ 结构与全部常数已实锤，未落 C++ |
| `0x4CACB0` / `0x4CAD00` sin/cos | ✅ **已落 C++ 并对拍** |
| `0x59E740` 桥 | ⬜ 仅提纲，见 §六 TODO |
| `0x5A08D0` 角度扩张 | ⬜ 仅提纲，见 §七 TODO |

未确认：

* `0x59D510` 候选起点 `P[1]` / `P[2]` 的 iso 恒等式（`P[0]` / `P[3]` 已对上）；
* `0x5A0160` 里 `work.owner == owner - 1` 这条「上一个编号」分支的语义
  （猜测与桥占用的那个编号有关，未验证）；
* `0x5A08D0` 的 `weight` 参数在公式中的位置。
