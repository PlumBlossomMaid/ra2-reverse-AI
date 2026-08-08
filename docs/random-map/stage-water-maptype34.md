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
  跌水阈值重新抽一遍，这是 RNG 总账里必须逐次记的。
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

**复刻状态：✅ 三个都已端到端对拍。**
`0x5A0700` 见 `rmg/regions.{h,cpp}` 的 `CollectBorderCells`（阶段 3 的
`regions` 用例已覆盖）；`0x5A0410` / `0x5A0160` 见同两个文件里的
`Erode` / `Dilate`，基准 `code/randmap/dump_region_ops_golden.py` →
`memory/data/rmg/region_ops_golden.txt`（16 + 17 = 33 例），ctest 名
`region_ops`。基准是整函数模拟执行，`0x5A0700`、`0x5657A0` 的哑元退化、
`0x486380` 未定砖谓词、`0x4865D0` 水岸谓词全是原身，一个桩都不留。

两个算子**都不取随机数**（通篇没有一条 `call 0x4CA1D0`），生成脚本里用
`assert self.rng_calls == 0` 守着这一条。

### `0x5A0700 CollectBorderCells(this; owner)`

返回一个 `DynamicVector<packedCoord>*`（vtable `0x7E3890`），装的是
**所有 `work.owner == owner`、且 8 邻域里存在一个界内邻居其 owner 不同**的格子。

* 出界的邻居**不算**「不同」——贴着地图边缘的格子不会因此被当成边界。
* 容量增长步长 `vec->+0x14 = this->+0x184 * this->+0x180`（地图面积）。
* 另外两个调用点：`0x58CAD7`（Init regions）与 `0x5A0A58`（在 `0x5A08D0` 里）。

### `0x5A0410 Erode(this; owner, rings, newOwner)` — `ret 0xC`

把编号为 `owner` 的连通块**向内剥 `rings` 圈**。每剥掉一格：

```c
work.owner    = newOwner;         // 0x5A04CA，工作数组为空指针时跳过
cell->IsoTile = 0;                // 0x5A04DD
cell->SubTile = 0;                // 0x5A04E0
cell->Level   = this->level;      // 0x5A04EE，+0x30C
```

每圈是**倒着**遍历当前边界向量的（`0x5A0498 dec esi` 起，`jns` 收尾）。
下一圈 = 当前圈八邻域里 `work.owner` 仍等于 `owner`、且本圈没被戳过的格子；
向量增长步长 = 当前圈的格数（`0x5A0539`）。最后一圈不再往里找
（`0x5A04FF cmp ring, rings-1 / jge`）。**恒返回 true**（`0x5A06E8 mov al,1`）。

`work.+0x3C` 被借用作「本圈已访问」的戳，值是 `ring + 1`；⚠️ 只有
`rings > 1` 时才在开头（`0x5A0436`）和结尾（`0x5A0693`）各按地图迭代顺序
清一遍——`rings <= 1` 时 +0x3C 里上一轮的残留会**原样留到函数返回之后**，
这是能被后续代码读到的可观测差异（对拍用例 `e-dirty-r1` / `e-dirty-r3`
就是盯着这一条造的）。

至于那个戳本身：它只影响下一圈向量里**有没有重复项**。重复项被重复剥一次
写的是同一批常数，所以**产物上看不出来**——已用突变测试确认（把去重条件
改成恒假，33 个 ctest 目标一个都不红）。这里照抄它，是为了让向量长度与原版
一致，也免得留下一处「看起来能省」的诱惑。

### `0x5A0160 Dilate(this; owner, rings, rectX, rectY, rectW, rectH, flag, cellVal)` — `ret 0x20`

把 `owner` 向外长 `rings` 圈，限制在给定矩形内。接受一个邻居的条件：

```c
   (work.owner == 0 && IsUndecidedTile(cell))
|| (flag != 0 && work.owner == owner - 1 && IsWaterOrShore(cell))
```

接受则 `work.owner = owner`；若 `flag != 0` 且该格未定砖，还写
`cell->Level = cellVal`。

接受则 `work.owner = owner`（`0x5A0348`）；若 `flag != 0` 且该格**仍是**
未定砖，还写 `cell->Level = (int8)cellVal`（`0x5A036B`，为此又取了一次格子）。

**关键出口**（`0x5A02EF cmp edi,[esp+0x30] / jne 0x5A03DE`）：任何一个
**没被接受**、且 `work.owner != owner` 的邻居都会让函数**立刻返回 false**。

> ⚠️ 早先这里写的是「owner 非 0 …… 的邻居」，**不对**。`work.owner == 0`
> 但砖不是未定砖（比如已经铺好的水砖、或普通地砖）同样落到这条出口上——
> 0 也不等于 `owner`。对拍用例 `d-solid-false`（全图普通砖）与
> `d-wall-false`（一圈砌死的普通砖）就踩的这一条。

被矩形夹掉、或被菱形判定挡掉的邻格是 `continue`，**不是**失败
（用例 `d-rect-empty`：矩形宽高为 0，一格都进不去，照样返回 true）。
所以调用方用它做「我这块有没有和别人贴上」的隔离测试。
下一圈的容量步长是 `newVec->+0x14 = cur->count * 3`。

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

### 河宽与跌水阈值

```c
wmax  = _ftol( max(1.0, WaterAmount * 0.07) );      // [0x7EDA20]=0.07, [0x7E1718]=1.0
width = PickRange(1, wmax);                          // 内联：_ftol(u*wmax + 1.0)，> wmax 重抽
half  = width / 2;                                   // 截断
widthLo = width - half;   widthHi = width + half;

fallThreshold = PickRange(35, 125);                  // _ftol(u * 91*2^-32 * ... + 35.0)
                                                     // [0x7EDA18]=91*2^-32, [0x7EDA10]=35.0，>125 重抽
```

`fallThreshold` 是「河至少流这么多步才允许出现瀑布」的门槛，见下。

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

### 跌水（瀑布）

```c
if (!isBranch && (flagA || flagB) && fallCount < 1
    && |_ftol(θ − θ₀)| < 0.7853981633974483   // [0x7E3D88] = π/4  ←— 见下的 bug
    && this->+0x310 != 0                      // 就是它，见下
    && steps > fallThreshold)
{
    dir = flagA ? (c <= 0 ? 6 : 2) : (s <= 0 ? 4 : 0);
    PlaceWaterfall(blobId, &coordA, &coordB, dir, &okOut, &x, &y);   // 0x59E740
    if (okOut) { fallCount++; this->+0x308 += 1; }
}
```

**`MapSeedClass +0x310` 的用途至此实锤**：它是 Init random map（`0x59A4C7`）
里那个 25% 的抛硬币结果，唯一的读取点在这里——
**它决定这张图的河上允不允许出现瀑布**。
（此前文档里只记到「唯一读者 `0x59DCFC`」，那正是 `0x59D510` 的内部地址。）

#### 一个真 bug：π/4 被 `_ftol` 压成了 1 弧度

`0x59DCCB`–`0x59DCF6` 那段的原文是：

```
0059dccb  fld  qword [esp+0x88]      ; θ 当前角
0059dcd2  fsub qword [esp+0xb8]      ; − θ₀ 初始角
0059dcd9  call 0x7c5f00              ; _ftol   ←— 先截成整数！
0059dcde  cdq / xor eax,edx / sub eax,edx      ; abs
0059dce4  mov  [esp+0x38],eax
0059dce8  fild dword [esp+0x38]      ; 再转回 double
0059dcec  fcomp qword [0x7e3d88]     ; 与 π/4 = 0.7853981633974483 比
```

差值被 `_ftol` **先向零截断成整数**，再拿去和 `π/4` 比。整数只可能是
0, ±1, ±2, …，而 `0 < 0.785 < 1`，于是这个判断的实际语义是

```
|θ − θ₀| < 1.0 弧度      // 而不是源码作者想写的 π/4 ≈ 0.785 弧度
```

门槛被**放宽了 27%**。这是标准的「本该 `fabs` 却写成整数 `abs`」型笔误：
作者要的是绝对值，顺手用了整数 `abs` 的惯用法，于是白白经过一次截断。
按「bug 也要复刻」的原则原样保留——判据写成
`std::abs(TruncToInt(θ - θ0)) < 0.7853981633974483` 而不是 `std::fabs(θ - θ0) < π/4`。

### 支流

```c
if (u < 0.01 && ok && !isBranch && fallCount == 0) {         // [0x7E3808] = 0.01
    // 截尾正态：mean = angle + π/2, sigma = π/6, 区间 [angle + π/6, angle + 5π/6]
    //           [0x7EDA08] = 2.6179938779914944 = 5π/6
    isBranch = 1;
    River(&here, branchAngle, 1);        // 递归
}
```

支流**不会**再生支流（`isBranch` 已置 1），也不会有瀑布。

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

if (fallCount == 0 && ok && this->level == 4) {             // +0x30C
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

**没出现瀑布的河会被下切成峡谷**：`+0x30C == 4`（当前地形层高为 4）时，
有 70% 的概率把河**两岸**整体抬高 4 层，河自己留在低处。
已经有瀑布的河则不再整体下切——它的落差已经由 `0x59E740` 就地做掉了
（那里是把河**自己**那一段抬高 4，见 §六），两套抬升叠在一起会得到 8 层的怪东西。

失败则回滚所有 `work.owner == blobId` 的格子；若已经落过瀑布，
连 `blobId - 1` 的格子一起回滚——瀑布在成功时做了 `+0x308 += 1`，
上游那半条河留在旧编号里。

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

## 六、`0x59E740`：瀑布 / 河道落差

```c
char __thiscall PlaceWaterfall(this; int blobId, CellStruct* a, CellStruct* b,
                               int dir, char* okOut, double* x, double* y);
```

> ⚠️ **更正**：这个函数早前（含 commit `5eef1b9` 的提交信息与文档初稿）被我
> 记成了「架桥」。**它不是桥，是瀑布**。判据是它取砖的**唯一**来源就是四个
> 瀑布 TileSet 全局，没有任何 `BridgeSet` 相关引用：
>
> | `dir` | TileSet 全局 | ini 键名（见 [stage-recalc.md](stage-recalc.md)） |
> | --- | --- | --- |
> | 0 | `[0xAA10A0]` | `WaterfallNorth` |
> | 2 | `[0xAA073C]` | `WaterfallEast` |
> | 4 | `[0xAA1050]` | `WaterfallSouth` |
> | 6 | `[0xABB110]` | `WaterfallWest` |
>
> 四个全局与键名的对应是 `dump_tileset_globals.py` 独立跑出来的，
> 与本函数的 switch 分支一一吻合。误判的成因记在 §八末尾。

`dir ∈ {0, 2, 4, 6}` 就是河当前的行进方向（东/南/西/北），
由调用方按 `cos`/`sin` 的符号选出。整个函数分十二步：

### 1. 先把出参清零

```c
*okOut = 0;
```

后面每一条 `return 1` 的早退路径都**不再动它**，所以调用方看到的是
`ok = 0`（「此处不适合放瀑布，跳过，但别把整条河判失败」），
而函数返回值 `1` 只是内部约定。这个「返回值与出参不一致」的写法与贴砖器
`0x57B440` 是同一种风格（那里也有一条返回 `false` 却不动 `*ok` 的出口）。

### 2. 按朝向拼出四个矩形

一个大 switch 填出：

* **净空矩形**（`local_110 / local_118` 行范围 + `local_a0 / local_fc` 列范围）
  —— 要求这一片是空地；
* **水面矩形 #1** `local_c8[4] = {x, y, w, h}`；
* **台地矩形** `local_b8[4] = {x, y, w, h}` —— 交给角度扩张用；
* **原点** `local_ec`（打包 `int16` 坐标）；
* **水面矩形 #2**（`local_d8 / local_d0` 列 + `local_d4 / local_cc` 行）。

四个 case 的常数（以 `dir = 0`、`a`/`b` 为河的横截面两端为例）：

```
净空：行 [b.Y - 12, b.Y);  列 [a.X - 2, a.X - 2 + (b.X - a.X) + 5)
水 #1：{a.X, b.Y - 4, (b.X - a.X) + 1, 4}
台地：{0, b.Y - 4, 0x200, 0x200 - (b.Y - 4)}
原点：(a.X, b.Y - 4)
水 #2：列 [a.X - 2, ...+8)、行 [b.Y - 12, ...)
```

`0x200 = 512` 是坐标空间上限，与阶段 2 其它地方传的 `{0,0,0x200,0x200}` 同源。

### 3. 净空扫描 —— 唯一的「此处不能放」判据

```c
for (每个净空矩形内、且落在菱形内的格 c) {
    if (work[c].owner != 0) return 1;          // *okOut 仍是 0
    if (!IsUndecidedTile(cell(c))) return 1;   // 0x486380
}
```

注意它**没有**任何随机数消耗，所以「跳过」不会扰动 RNG 流——
这一点对总账很重要。

### 4. 涂第一片水（**每格 2 次取数**）

```c
for (每个水面矩形 #1 内、且在菱形内的格 c) {
    r = PickBelow(6);                    // Random(); _ftol; while (r > 5) 重抽
    cell(c)->IsoTile = [0xAA0738] + r;   // WaterSet 起始砖号 + r
    s = PickBelow(4);                    // 同上，> 3 重抽
    cell(c)->SubTile = s;                // +0x11A
    work[c].owner   = blobId;            // +0x38
}
```

**这是整条河里 RNG 密度最高的地方**：两次拒绝采样、每格两个随机数。
`PickBelow(6)` 只用到 WaterSet 的前 6 块砖（`TilesInSet = 0x0E` 共 14 块），
而 `PickBelow(4)` 的子格号意味着这些是 2×2 砖——与
[README](README.md#) 里记的「水砖恰是前 8 块 2×2 + 后 6 块 1×1」对得上：
前 6 块必然是 2×2，取 `SubTile ∈ [0,4)` 合法。

> 注意这里**没有走贴砖器 `0x57B440`**，是直接写 `CellClass +0x38 / +0x11A`。
> 所以砖的占地、空洞、高度增量统统不生效，纯粹是「这一格显示这个子格」。

### 5–7. 把落差区扩出来

```c
ok = AngularGrow(this->blobId, 0.003f /*0x3B449BA6*/, plateauRect, &origin, 0);  // 0x5A08D0
if (ok) ok = Shoreline(this->blobId, 0);                                        // 0x57A0C0
if (ok) ok = Dilate(this->blobId, 2, plateauRect, 0, 0);                        // 0x5A0160
```

三步串成短路链，任一步失败后面就都不做（但已经涂下去的水**不回滚**）。
`weight = 0.003f` 比河流收尾用的 `0.01f` 小三倍多，按 §七的公式
`n = _ftol(0.5·max(ln k, 1)/weight)` 意味着**迭代次数多三倍多**——
瀑布上游要平出一大片台地。

注意这三个调用传的都是 `this->+0x308`（当场读），不是入参 `blobId`；
调用点上二者相同，但复刻时要照着读，否则将来若有别的调用方就会分叉。

### 8. 台地整体抬高 4 层

```c
for (每个格 c 走一遍全图迭代器) {
    if (work[c].owner == this->blobId) cell(c)->Level += 4;   // +0x11B
}
```

第 4 步涂的那片水**也在** `blobId` 里，所以它跟着被抬到高处——
它是瀑布**上游**的河面。

### 9. 涂第二片水（**又是每格 2 次取数**）

与第 4 步逐字相同（同样的 `PickBelow(6)` + `PickBelow(4)` + `owner = blobId`），
只是矩形换成水面矩形 #2。这一片在抬升**之后**涂，所以留在低处——它是**下游**河面。

上下游两片水中间那道 4 层的陡坎，就是瀑布要贴砖的地方。

### 10. 贴四块瀑布砖

每块砖走的都是同一套三段式：

```c
DAT_00880990 = TileTypes[ WaterfallXxx + k ];     // [0xA8ED2C] 是 TileTypeClass 数组基址
Map->SetTargetCell(&outPrev, &coord);             // 0x4A91B0 → 写 MapClass +0x1174
PlaceTile(0, 0x10000, level, this->blobId, &ok, 0);   // 0x57B440
```

`[0x880990]`（砖对象）与 `MapClass +0x1174`（落点）这对交接方式，
与岸线平滑 `0x57B189` 处**逐条相同**（`mov ecx, 0x87F7E8` + 两个栈参、
回吐的旧值当场丢弃），详见
[stage-seeding-water.md](stage-seeding-water.md#选中的砖是怎么交出去的两个全局)。
`level` 是从坎顶某一格读来的 `cell->Level`（`+0x11B`），四块砖共用。

四块砖的**使用顺序是 `[+0]`、`[+3]`、`[+2]`、`[+1]`**（字节偏移 0 / 0xC / 8 / 4）：

1. `[+0]` 贴在坎顶；
2. `[+3]` 贴在坎底；
3. 中间那段用一个循环交替贴 `[+2]`（跨 2 格）与 `[+1]`（跨 1 格），
   直到把宽度走完：

```c
int i = 0, rem = span;
while (i < span) {
    if ((rem & 1) == 0) { coord = ...(i + 2); i += 2; rem -= 2; tile = set[+2]; }
    else                { coord = ...(i + 1); i += 1; rem -= 1; tile = set[+1]; }
    PlaceTile(...);
}
```

那个 `rem & 0x80000001` + 负数折回的奇偶判定，与 §五 sin/cos 里
`& 0x80001FFF` 是同一个编译器惯用法（13 位换成 1 位），
`rem` 恒非负所以照样是白写的。

夹在中间还有几处零碎的直接写：坎沿两格被写成 `IsoTile = 0xFFFF`（未定砖哨兵）、
`Level += 4`、`SubTile = 0`、`work.owner = blobId`；
另有一小段循环把坎底一排格子 `Level -= 4` 抵消掉第 8 步的整体抬升。

### 11. 河流「瞬移」12 格

```c
static const int dx[4] = {  0, 12,  0, -12 };
static const int dy[4] = { -12,  0, 12,   0 };
if (ok) { *x += dx[dir / 2];  *y += dy[dir / 2]; }
```

**这是复刻时最容易漏的一条**：瀑布成功之后，调用方 `0x59D510` 的游标
`(x, y)` 被**直接推进 12 格**，河道在瀑布处断开、从坎底重新开始画。
12 正好是净空矩形的纵深。

### 12. 出参

```c
*okOut = ok;
return ok;
```

只有走到底才写 `*okOut`；前面所有早退都让它保持第 1 步的 `0`。

### RNG 总账影响

| 位置 | 每格取数 | 说明 |
| --- | --- | --- |
| 第 4 步水面 #1 | 2 次（各含拒绝采样） | `PickBelow(6)` + `PickBelow(4)` |
| 第 9 步水面 #2 | 2 次 | 同上 |
| `AngularGrow`（§七） | 见 §七 | 种子/邻居各 1 次 + 每轮一个正态偏差 |
| `Shoreline` | 见岸线平滑 | 本身就是阶段 2 的取数大头 |

其余步骤（净空扫描、抬升、贴砖、瞬移）**一次都不取**。

## 七、`0x5A08D0`：角度优先的块扩张（AngularGrow）

```c
char __thiscall AngularGrow(this; int blobId, float weight,
                            int rect[4], CellStruct* origin, char flush);
```

两个调用点：河流收尾的「下切成峡谷」（`weight = 0.01f`）与瀑布的台地
（`weight = 0.003f`）。它是本支路里唯一按**方位角**而不是按距离扩张的算子。

### 分配

```c
int cap = max(2 * this->+0x180 * this->+0x184, 100);   // 地图面积 ×2，下限 100
nodes = new Node[cap];        // 每个 Node 8 字节：{packedCoord, float prio}
heap  = new Heap;             // 5 个 int：{count, cap, storage, maxPtr, minPtr}
heap->cap = cap;  heap->storage = new void*[cap + 1];   // 逐项清零，含第 cap 项
heap->maxPtr = 0;  heap->minPtr = -1;
```

`new` 失败只是把指针写成 0 然后继续跑（西木惯例，没有异常处理）。

### 基准角

```c
double base = 0.0;
if (rect.x == 0) { if (rect.w != 0x200) base = π;        }   // [0x7E44D0]
else             {                      base = 0.0;      }
if (rect.y == 0) { if (rect.h != 0x200) base = π/2;      }   // [0x7E2820]
else             {                      base = 3π/2;     }   // 4.71238898038469
```

两组 `if` 是**顺序执行**的，第二组无条件覆盖第一组的结论——
只要 `rect.y != 0`，基准角一定是 `3π/2`，第一组白算。
瀑布传的四个矩形里 `dir = 0/2` 的 `rect.y != 0`，走的正是这条覆盖路径。
照抄。

「扩张方向」的语义：矩形贴着坐标空间的哪条边，就朝反方向长。
`0x200` 是坐标上限，`rect.x == 0 && rect.w == 0x200` 表示「这一维没被裁」，
于是不给角度。

### 清账 + 种子

```c
for (每格) work[c].+0x3C = 0;                 // 入堆标记，全图清零
vec = CollectBorderCells(blobId);             // 0x5A0700
k   = vec->count;                             // ★ 迭代次数公式里的 k
for (每个 c ∈ vec) {
    if (!(rect.x <= c.X < rect.x + rect.w && rect.y <= c.Y < rect.y + rect.h)) continue;
    a = AngleDev(c);                          // 见下
    u = (uint32)Random() * 2^-32;             // [0x7ED898]
    prio = (float)(2.0 * u + 1.5 * a);        // [0x7ED790]=2.0 [0x7ED798]=1.5，皆 double
    HeapPush(node);
}
vec->~DynamicVector();                        // 虚析构
```

两个系数在 `.rdata` 里是 **double**，消费指令是 `fmul qword ptr`——
按 float 读会读成 `0.0`（这个坑踩过一次）。乘法在 x87 扩展精度里做，
最后 `fstp dword` 落成 float 存进节点。

`AngleDev` 是「这一格相对原点的方位角，离基准角偏了多少」：

```c
int dx = c.X - origin.X;
double a;
if (dx == 0) a = π/2;                                   // [0x7E2820]
else {
    a = atan( -(double)(c.Y - origin.Y) / (double)dx );  // FUN_004CADE0
    if (dx < 0) a += π;                                 // [0x7E44D0]
}
a = fabs(a - base);
while (a >= 2π) a -= 2π;                                // [0x7E3CC0]
if (a > π) a = 2π - a;                                  // 折进 [0, π]
```

分子取负是等距屏幕坐标 Y 朝下的老规矩，与河流的 `y -= Sin(θ)` 同源。
注意先 `fabs` 后模 `2π`：`fabs` 的结果非负，所以 `while` 一定会终止；
但若 `a - base` 落在 `(-2π, 0)`，`fabs` 已经把它镜像过去了——
这不等于「取角度差的主值」，是**另一个函数**。照抄。

### 迭代次数 —— `weight` 的确切位置

`0x5A0C40`–`0x5A0C74`：

```
005a0c40  fldln2
005a0c42  fild dword [esp+0x10]      ; k = CollectBorderCells 返回的格子数
005a0c46  fyl2x                      ; ln(k)
005a0c48  fld  qword [0x7e1718]      ; 1.0
005a0c4e  fcomp st(1)                ; 1.0 <= ln(k) ? 保留 ln : 换成 1.0
005a0c5f  fdivr qword [0x7e1718]     ; L = max(ln k, 1.0)  →  1.0 / L
005a0c65  fmul dword [ebp+0xc]       ; * weight            →  weight / L
005a0c68  fdivr dword [0x7e2ac8]     ; 1.0f / (weight/L)   →  L / weight
005a0c6e  fmul dword [0x7e5168]      ; * 0.5f
005a0c74  call 0x7c5f00              ; _ftol
```

即

```c
double L = max(ln(k), 1.0);
int    n = _ftol(0.5 * L / weight);
int   n2 = PickBelow(n / 2 + 1);      // Random(); _ftol; while (r > n/2) 重抽
// 主循环跑 n + n2 轮
```

`1.0 / L` 再 `* weight` 再 `1.0f / (...)` 这一串**绕了一圈回到 `L / weight`**——
是源码写成 `1.0f / (weight / L) * 0.5f` 的直译，两次倒数在数学上抵消，
但在浮点上**不抵消**（两次除法各引入一次舍入），所以必须照这个顺序算。
另外 `[0x7E2AC8] = 1.0f` 与 `[0x7E5168] = 0.5f` 是 **float**，
而 `[0x7E1718] = 1.0` 是 **double** —— 三个常数的读宽度都得跟着指令走。

代入两个调用点（设边界格数 `k ≈ 600`，`ln 600 ≈ 6.397`）：

| 调用点 | `weight` | `n` |
| --- | --- | --- |
| 河流下切 `0x59D510` | `0.01f` | ≈ 319 |
| 瀑布台地 `0x59E740` | `0.003f` | ≈ 1066 |

**`weight` 越小扩张越大**，它是「每一轮扩张的代价」而不是「扩张量」。
`k = 0` 时 `fyl2x` 给 `-inf`，`max(-inf, 1.0) = 1.0`，`n = _ftol(0.5/weight)`；
但 `k = 0` 同时意味着堆是空的，主循环第一轮就 `local_50 == NULL` 而 break。

### 主循环

进循环之前**先弹一次**堆（`cur = HeapPop()`），然后：

```c
for (int i = 0; i < n + n2; ++i) {
    if (!ok || cur == NULL) break;
    // 认领
    if (work[cur].owner == 0 && IsUndecidedTile(cell(cur))) {
        work[cur].+0x4B  = 1;
        work[cur].owner  = blobId;
    }
    // 八邻域
    for (int d = 0; d < 8; ++d) {
        c = Neighbor(cur, d);                       // 0x42D510 + 表 [0x89F688]
        if (!InDiamond(c)) continue;
        if (work[c].owner == 0 && work[c].+0x3C != blobId
            && rect 包住 c && IsUndecidedTile(cell(c))) {
            a    = AngleDev(c);
            u    = (uint32)Random() * 2^-32;
            prio = (float)(2.0 * u + 1.5 * a);
            work[c].+0x3C = blobId;                 // 打入堆标记
            HeapPush(node);
            continue;                               // ★ 入堆成功就不做下面的判据
        }
        if (work[c].owner == 0) continue;           // 未归属但被别的条件挡下：忽略
        if (work[c].owner != blobId) ok = false;    // 撞上第三方地盘
    }
    base += NormalDeviate() * 0.7853981633974483;   // [0x7E3D88] = π/4
    cur = HeapPop();
}
```

**基准角每轮都在随机游走**（截断正态 × π/4，无区间限制），
于是「朝哪边长」这件事本身是布朗运动的——这与涂块核心 `0x59BBC0`
让引力中心随机游走是同一个设计思路。

节点数组按 `local_48`（已入堆总数）线性推进，**从不回收**；
`cap` 是地图面积的两倍，越界了也不检查（堆的 `count + 1 < cap` 那道门槛
顺带兜住了，见涂块核心里记的同一条怪癖）。

### 收尾

```c
if (flush != 0) {
    while ((cur = HeapPop()) != NULL && ok) {
        if (work[cur].owner == 0) work[cur].owner = blobId;   // 无条件认领，不查砖
        else if (work[cur].owner != blobId) ok = false;
    }
}
delete[] nodes;   // 0x7C8B3D
delete heap;      // 0x5AC340
return ok;
```

河流收尾传 `flush = 1`（把堆里剩的全收编），瀑布传 `0`（不收）。
注意 flush 分支里**不查 `IsUndecidedTile`**，与主循环的认领条件不一致。

### `FUN_004CADE0` = 查表 atan（已关闭）

它确实是反正切，而且**和 `FastSqrt` / `Sin` 是同一套「西木快速数学库」——纯查表**：

```c
k = abs(_ftol(x / 0.024413354694843292f));         // 步长常量 [0x8650B8]
r = (k >= 0x1001) ? 1.5707963705062866f            // float π/2 [0x7E897C]
                  : atanTable[k];                  // float[0x1001] @0x8610B4
return (float)x < 0.0f ? -r : r;
```

4097 项表已导出并**用还原出的生成式逐位重算通过**，C++ 复刻落在
`code/rewrite/rmg/fast_math.cpp` 的 `Atan`，105 组 Unicorn 黄金值全绿。
完整取证见 [fast-math.md](fast-math.md) §四之三。

对本函数（`AngleDev`）来说，有两个后果必须记住：

1. **量化的是正切值而不是角度。** `dy/dx` 落在 `[k·h, (k+1)·h)` 的都给同一个角。
   `dx` 大、`dy` 小时精度极高；`|dy/dx|` 接近 1 时一档就跨 1.4°——
   `atan(1)` 原版给 `0.7735263`，比真值 π/4 低 **0.68°**。
   于是「45° 方向」在原版眼里根本不是 45°；
2. **`|dy/dx| > 100.0215` 时直接给 float π/2，且与表尾差 0.01 弧度**。
   `dx == 0` 那条特判用的是 double π/2 `[0x7E2820] = 1.5707963267948966`，
   而越界出口给的是 float `1.5707963705062866`——**两条"垂直"路径的值不相等**。
   `dx = ±1、|dy| ≥ 101` 的边界格会踩到这个不连续点。

* `FUN_005AD870`（堆下沉）与 `FUN_005AC340`（堆析构）的并列语义有没有和
  涂块核心那个最小堆一致（那边实锤是「严格大于才交换」）——形态相同，未逐条比对。

## 八、复刻状态与未确认清单

| 件 | 状态 |
| --- | --- |
| `0x59C580` 调度 | ✅ 已实锤，未落 C++ |
| `0x59C920` 湖 | ✅ 已实锤（预算/禁区/起点/正态/优先级/回滚），未落 C++ |
| `0x5A0700` / `0x5A0410` / `0x5A0160` | ✅ 已实锤，未落 C++ |
| `0x59D510` 河 | ✅ 结构与全部常数已实锤，未落 C++ |
| `0x4CACB0` / `0x4CAD00` sin/cos | ✅ **已落 C++ 并对拍** |
| `0x59E740` 瀑布 | ✅ 十二步结构与四个 TileSet 已实锤，未落 C++ |
| `0x5A08D0` 角度扩张 | ✅ 结构 + 迭代次数公式已实锤，未落 C++ |
| `0x4CADE0` atan | ✅ **已落 C++ 并逐位对拍**（含表的生成式） |

未确认：

* `0x59D510` 候选起点 `P[1]` / `P[2]` 的 iso 恒等式（`P[0]` / `P[3]` 已对上）；
* `0x5A0160` 里 `work.owner == owner - 1` 这条「上一个编号」分支的语义。
  §六第 11 步给了一条线索：瀑布成功时 `+0x308 += 1`，于是**上游河段的编号
  恰好是当前编号减一**，`Dilate` 的这条分支很可能就是为「让下游把上游认作自己人」
  准备的。仍未直接验证；
* `0x5A08D0` 的两个堆辅助 `FUN_005AD870` / `FUN_005AC340` 是否与涂块核心
  那个最小堆的并列语义一致。

### 一条自我更正的记录

`0x59E740` 在本文初稿（及 commit `5eef1b9` 的提交信息）里被写成了「架桥」。
成因是**从调用点的语境反推函数用途**：它出现在河流生成器里、由一个
25% 抛硬币的全局开关控制、成功之后河的下切被取消——
「有桥的河不挖成峡谷」听上去太顺了，于是没有去核对它到底取的是哪套砖。

实际去看取砖点，四个分支引用的是四个瀑布 TileSet 全局，一处
`BridgeSet` 都没有。**语境合理不能替代取证**——这正是 CLAUDE.md
「证据优先」那一条要防的情况，记在这里备忘。
