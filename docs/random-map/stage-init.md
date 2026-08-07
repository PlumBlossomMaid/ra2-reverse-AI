# 阶段：Init random map（阶段 1）

> 状态：**取数点已实锤（全阶段只抽 1 个随机数）**，与地图状态相关的动作已梳理；
> 大量引擎侧初始化（HouseClass / Rules / 显示层）与地形生成无关，不在本文范围。
> 实现函数 `FUN_00599650`（4124 字节，MapGen.cpp 里最大的一个）。

## 一、它其实不生成任何地形

`RMG: Init random map` 干的是"把一张空白 scenario 搭起来"：写内存 INI、
布光、重建 HouseClass/Rules、清空并重置全图格子、把工作数组开出来。
真正的地形要到阶段 2 `Seeding water` 才开始长。

对复刻而言，这个阶段只有三件事重要：

1. **它消耗且仅消耗 1 个随机数**（下面第二节）——随机流的第一笔账
2. **全图格子与工作数组的初值**——后面所有阶段都建立在这套初值上
3. **地图真实尺寸 `W`/`H` 就是在这儿算出来的**（§三之二）——
   `.sed` 里的 `Width`/`Height` 是 0..3 的档位而非格数，
   后续每个阶段公式里的 `W`/`H` 全部来自这一步

## 二、唯一的取数点：`0x59A4C7`，一次 25% 抛硬币

全函数只有一处 `Randomizer::Random`，而且 `this` 明确是生成器自己的
`0xABE890`（不是引擎那个 Randomizer）：

```asm
0059a4b6  mov  ecx, 0xabe890
0059a4c7  call 0x65c780              ; Randomizer::Random
0059a4cc  mov  [esp+0x18], eax       ; 低 32 位
0059a4d0  mov  [esp+0x1c], ebx       ; 高 32 位 = 0 → 当无符号用
0059a4d4  fild qword ptr [esp+0x18]
0059a4d8  fmul qword ptr [0x7ed898]  ; × 1/4294967295.0
0059a4de  fld  dword ptr [0x7ed630]  ; 0.25f
0059a4e4  fcompp
0059a4e8  test ah, 0x41
0059a4eb  jne  0x59a4f4
0059a4ed  mov  eax, 1                ; u < 0.25
0059a4f2  jmp  0x59a4f6
0059a4f4  xor  eax, eax
0059a4f6  mov  [edi+0x310], al       ; MapSeedClass +0x310
```

即：

```c
bool flag310 = UniformUnit() < 0.25;      // UniformUnit 见 fast-math.md
```

`fild qword` + 高位置 0 是那套标准的"无符号 32 位转 double"惯用法
（见 [fpu-and-random-idioms.md](fpu-and-random-idioms.md)）。

`MapSeedClass +0x310` 由构造函数 `0x5958C8` 置 0，全 exe 只有 `0x59DCFC`
一处读它——落在"山丘/地形装饰"那一段（`0x59D51D`–`0x59E740` 区间内），
**这个 25% 开关到底开的是什么，未确认**。

> 记账要点：**阶段 1 恰好消耗 1 个 `Random()`**，而且是在阶段的**最后**才抽
> （紧接着就是雷达图刷新与灯光参数写入）。复刻时必须在同一位置抽，
> 否则整条流后移一位。

### 别被引擎侧的随机数骗了

对 `FUN_00599650` 做纯调用树传递闭包会得到"可达 2557 个函数、53 处取数点"——
因为它会调进 `ScenarioClass` 初始化、`HouseClass` 构造、显示层……那些地方
**用的是引擎自己的 Randomizer 对象**，与 `0xABE890` 是两份独立状态。
判定必须以取数点的 `this` 为准，详见
[fpu-and-random-idioms.md](fpu-and-random-idioms.md)「这个 Randomizer 只被 MapGen 自己用」。

## 三、写进内存 INI 的东西

阶段开头把一批键值写进一个内存 `INIClass`（后面 `INISection::~INIClass` 收尾）：

| 段 | 键 | 值 |
|----|----|----|
| `Map` | `Theater` | `"TEMPERATE" + Theater*0x70`（字符串表按 0x70 步长） |
| `Map` | `Size` | `{0, 0, W+4, H+12}`（`local_7c`，见 §三之二） |
| `Map` | `LocalSize` | `{2, 5, W, H}`（`local_8c`，X/Y 是写死的 2 和 5） |
| `Map` | `Level` | `MapSeedClass +0x30C`，此处被写死为 **4** |
| `Basic` | `Player` | 取自 `[0xA83C9C]->+0x24` |
| `Lighting` | `Ambient` / `IonAmbient` | `预设表[Time] × [0x7E3808]`（`Time` = 参数 `+0x48`） |
| `Lighting` | `RedTint`/`GreenTint`/`BlueTint` | 均 `1.0` |
| `Lighting` | `IonRed`/`IonGreen`/`IonBlue` | `0.3` / `0.4` / `0.75` |
| `Lighting` | `Ground` / `IonGround` / `IonLevel` | `0` |
| `Lighting` | `Level` | `预设表[Time] / 100`（整数除） |

`Theater == 0` 与否会切换取哪一组预设表指针（`+0x1A8` vs `+0x1C4`，共 4 组 8 个指针），
即**两套 theater 各一份光照预设**，再按 `Time` 下标取值。这些表本身在 `.bss`，内容未确认。

## 三之二、地图到底有多大：`Width`/`Height` 参数**不是**格数

这是复刻绕不开的一步——之前所有阶段的公式里都写着 `W = [0x87F8DC]`、`H = [0x87F8E0]`，
但没人查过这俩全局是怎么来的。答案在阶段 1 最开头的 12 条指令里（`0x599665`-`0x599727`）：

```asm
00599665  fild dword ptr [edi + 0x64]        ; MapSeedClass.Width  ← 对话框滑条
00599668  mov  esi, dword ptr [edi + 0x50]   ; NumPlayers
0059966b  sub  esi, 2                        ; ← 下标 = NumPlayers - 2
0059966e  fmul dword ptr [0x7ed968]          ; × 0.3333333432674408f (1/3)
00599674  fstp dword ptr [esp + 0x14]        ; wt = Width / 3        （单精度！）
00599678  fild dword ptr [edi + 0x68]        ; MapSeedClass.Height
0059967b  fmul dword ptr [0x7ed968]
00599681  fstp dword ptr [esp + 0x10]        ; ht = Height / 3
```

随后按 `MapType` 分两路把 `wt`/`ht` 夹到 `[0x7E5190] = 1.2`（double）。
**注意 `ht` 的夹取结果不回写内存**——它一直留在 x87 栈上，供后面的高度插值用：

```asm
0059968a  mov  eax, [edi + 0x3c]             ; MapType
0059968d  cmp  eax, 3 / je  0x5996d7         ; MapType 3/4 跳过整段夹取
00599697  fld  dword ptr [esp + 0x14]        ; wt
0059969b  fcomp qword ptr [0x7e5190]         ; 与 1.2 比
005996a8  fld  dword ptr [esp + 0x14]        ;   wt <= 1.2 -> 取 wt
005996ae  fld  qword ptr [0x7e5190]          ;   否则      -> 取 1.2
005996b4  fstp dword ptr [esp + 0x14]        ; 夹取后的 wt 回落 float 并写回栈槽
005996b8  fld  dword ptr [esp + 0x10]        ; ht
005996bc  fcomp qword ptr [0x7e5190]
005996c9  fld  dword ptr [esp + 0x10]        ;   ht <= 1.2 -> 取 ht
005996cf  fld  qword ptr [0x7e5190]          ;   否则      -> 取 1.2   ← 留在 st(0)
005996d7  fld  dword ptr [esp + 0x10]        ; MapType 3/4 的落点：未夹取的 ht
```

三条路径（`0x5996CD` / `0x5996D5` / `0x5996D7`）都汇合到 `0x5996DB`，
且**都是把 `ht` 留在 x87 栈顶**——`0x5996D7` 加载 `esp+0x10` 不是笔误，
它就是 `ht` 本身，只是少了那次夹取。

最后是两条线性插值，各自 `_ftol`（`0x7C5F00`，RC=chop 截断）落成整数。
宽度那条从**内存栈槽**读 `wt`，高度那条用 **x87 栈上**那份 `ht`（`st(2)`）：

```asm
005996db  fild dword ptr [esi*4 + 0x7ed634]  ; wLo[NumPlayers-2]
005996e2  fld  dword ptr [0x7e2ac8]          ; 1.0f
005996e8  fsub dword ptr [esp + 0x14]        ; 1 - wt      ← 读内存里的 wt
005996ec  fmulp st(1)                        ; wLo * (1 - wt)
005996ee  fild dword ptr [esi*4 + 0x7ed650]  ; wHi[NumPlayers-2]
005996f5  fmul dword ptr [esp + 0x14]        ; wHi * wt
005996f9  faddp st(1)
005996fb  call 0x7c5f00                      ; _ftol
00599700  mov  [edi + 0x180], eax            ; +0x180 = 真实地图宽 W

00599706  fild dword ptr [esi*4 + 0x7ed66c]  ; hLo[NumPlayers-2]
0059970d  fld  dword ptr [0x7e2ac8]          ; 1.0f
00599713  fsub st(2)                         ; 1 - ht      ← 用 x87 栈上的 ht
00599715  fmulp st(1)
00599717  fild dword ptr [esi*4 + 0x7ed688]  ; hHi[NumPlayers-2]
0059971e  fmul st(2)                         ; hHi * ht
00599720  faddp st(1)
00599722  call 0x7c5f00
00599748  mov  [edi + 0x184], eax            ; +0x184 = 真实地图高 H
00599746  fstp st(0)                         ; 丢掉 ht，平衡 x87 栈
```

即：

```c
int idx = NumPlayers - 2;                       // 2..8 玩家 -> 0..6
float wt = Width  / 3.0f,  ht = Height / 3.0f;  // 单精度中间量
if (MapType != 3 && MapType != 4) {             // 3/4 完全跳过夹取
    wt = (float)min(wt, 1.2);                   // 夹完回落 float
    ht =        min(ht, 1.2);                   // 夹完留 double，见下
}
W = (int)(wLo[idx] * (1 - wt) + wHi[idx] * wt);   // _ftol 截断
H = (int)(hLo[idx] * (1 - ht) + hHi[idx] * ht);
MapSeedClass->+0x180 = W;   MapSeedClass->+0x184 = H;
```

#### 两个必须照抄的舍入细节

**其一：夹取后的 `wt` 和 `ht` 不是同一个数。** `wt` 经 `0x5996B4` 的 `fstp dword`
落回单精度，RC=chop 下 `1.2` 被截成 `1.1999999284744263f`；`ht` 那条路
（`0x5996CF`）加载完就 `jmp`，**一直留在 x87 栈上保持 double**
`1.1999999999999999556`。模拟执行读寄存器实锤：`Height=4` 时栈上尾数是
`0x9999999999999800`，正是双精度 1.2 而非单精度那份。

复刻时按汇编照抄。两条路径在当前取值域内**恰好给出相同的整数**（两个模型各跑
1260 例都 0 例不一致），但这是巧合，不是可以依赖的性质。

**其二：插值必须逐步舍入，不能用精确算术。** 这一条是真会算错的：

```
H = 70 × (1 - 1.2) + 80 × 1.2
```

精确有理数算是 `70 + 10 × 1.2 = 82`。但 double 的 1.2 是 `1.1999999999999999556`，
逐步 chop 之后得 `81.99999999999999`，`_ftol` 截断成 **81**。原版给的就是 81。

同理**舍入模式不能用就近**：全程 round-to-nearest 会在 1260 例里错 **420 例**。
`RC=chop` 在这里不是可有可无的形式主义。

> 建模方法：用 `fractions.Fraction` 精确表示，在每一步之后显式截断到 24 位 / 53 位
> 尾数（`fstp dword` 用 24，PC=53 的算术用 53），再与模拟器全网格对拍。
> 这样得出的结论与宿主 FPU 无关。

**复刻状态**：`code/rewrite/rmg/map_size.{h,cpp}`，
黄金对拍 `test/test_map_size.cpp` 覆盖 `NumPlayers 2..8 × MapType 0..4 × 档位 0..5`
共 **1260 例全绿**，基准存档 `memory/data/rmg/map_size_golden.txt`。

### 四张插值表（`.rdata`，各 7 项，下标 = `NumPlayers - 2`）

| 地址 | 表 | 2P | 3P | 4P | 5P | 6P | 7P | 8P |
|---|---|---|---|---|---|---|---|---|
| `0x7ED634` | `wLo` 宽·滑条最小 | 70 | 70 | 70 | 80 | 90 | 100 | 100 |
| `0x7ED650` | `wHi` 宽·滑条最大 | 80 | 80 | 80 | 90 | 100 | 110 | 120 |
| `0x7ED66C` | `hLo` 高·滑条最小 | 70 | 70 | 70 | 80 | 90 | 100 | 100 |
| `0x7ED688` | `hHi` 高·滑条最大 | 80 | 80 | 80 | 90 | 100 | 110 | 120 |

宽高两组表**逐项相同**——长宽差异只来自 `Width`/`Height` 两个滑条给出的不同 `wt`/`ht`。

于是 `.sed` 里那个 `Width=0 / Height=0` 就解释通了：**它不是格数，是 0..3 的档位**
（`/3` 之后正好铺满 `[0, 1]`）。2 人图 `Width=0` → `W = 70`，`Width=3` → `W = 80`。

### `MapType 3/4` 的真实差异：滑条能开到 1.2 档以上

夹取被跳过，于是 `wt`/`ht` 可以超过 1.2，插值变成**外推**，地图能比表里的
`Hi` 还大。模拟执行取真值（2 人图）：

| 档位 | `MapType=1`（夹取） | `MapType=3/4`（不夹） |
|---|---|---|
| 3 | 80 | 80 |
| 4 | 81 | 83 |
| 5 | 81 | 86 |
| 6 | 81 | 90 |

`MapType=1` 在档位 ≥ 4 后被 1.2 钉死在 81，而 3/4 一路线性外推。
不过 RMG 对话框的滑条上限**未确认**是否允许 > 3，若不允许则这条分支实际观察不到。

### ⚠️ 一条被推翻的推测（留档）

最初读汇编时我把 `0x5996D7` 的 `fld dword ptr [esp + 0x10]` 认成
"`MapType 3/4` 时宽度插值误用了 `ht`"，并当成复制粘贴 bug 写进了怪癖清单。
**模拟执行直接证伪**：固定 `Width=0` 只动 `Height`，`MapType=3` 下 `W` 始终是 70，
没有跟着 `Height` 走。错因是只看单条指令没跟 x87 栈——`esp+0x10` 在那里本来就该是 `ht`，
它是给**高度**插值准备的，宽度插值读的是内存里的 `esp+0x14`。

留下这段是因为它印证了一条方法论：**凡涉及 x87 栈的结论，必须模拟执行验证**，
"读一眼汇编觉得像 bug"在这个子系统里的误判率相当高。

### `side = W + H + 1`：工作数组边长的来源

`0x42AC00`（`0x685198`/`0x6851A7` 处传入 `0x87F8D4` 那个矩形）：

```asm
0042ac56  mov  ecx, dword ptr [eax + 0xc]    ; rect.Height
0042ac59  mov  edx, dword ptr [eax + 8]      ; rect.Width
0042ac5c  lea  eax, [ecx + edx + 1]
0042ac60  mov  dword ptr [0x89c2dc], eax     ; side = W + H + 1
0042ac65  imul eax, eax                      ; side²
0042ac68  lea  edi, [eax*4]                  ; 四个 side² 的 int 数组
```

`side = W + H + 1` 正是等距菱形的外接正方形边长（等距坐标 `X = rx+ry+1` 的上界），
**实锤了工作数组为什么是方阵**、以及 `stage-init.md` §五 `side²×0x50` 的分母。
同一函数顺带填了 8 个邻接步长全局（`0x89A304`-`0x89A320`）：
`{-side, 1-side, 1, 1+side, side, side-1, -1, -1-side}`——按 `side` 展平后的八邻域。

> 关于 `0x87F8D4` 那个矩形：它其实是 **`MapClass::Instance(0x87F7E8) + 0xEC`**
> 这个成员，`{X, Y, W, H}` 依次落在 `+0xEC/+0xF0/+0xF4/+0xF8`
> （详见 [map-geometry.md](map-geometry.md) §五）。赋值点是 `0x5662B0`：
> 从参数矩形拷 4 个 dword 进来之后**强行把 X/Y 清零**，只留 W/H——
> 这正是本节 INI 里 `Size={0,0,W+4,H+12}` 那个 `0,0` 前缀的由来。
> `0x68554F` 那处绝对写只是一次性清零，不是真正的赋值。
> 仍未跟完的只剩最后一跳：**谁把 `MapSeedClass +0x180/+0x184` 装进那个参数矩形**
> （只跟到 `0x685198` 那条地图重建路径），两者数值相等由下游全部公式的自洽性佐证。


## 四、全图格子重置

用地图格子迭代器（`Reset 0x578350` / `Next 0x578290`）扫全图，每格：

```c
cell->+0x11C = 0;
cell->+0x11B = 4;              // 注意是常数 4，不是 MapSeedClass 的 Level
cell->IsoTileTypeIndex = 0;    // +0x38
cell->+0x11A = 0;
cell->+0x44  = 0xFFFFFFFF;
cell->+0x11E = 0;
```

随后重设 waypoint：700 与 699（或 `[0xA8B230]+0x20C`）都指向地图中心
`(W/2 + H/2 + 1, W/2 + H/2)`，并给该格打上 `+0x140 |= 4`。

## 五、工作数组：`side² × 0x50`

```c
free([0xABED10]);
n = side * side;                       // side = [0x89C2DC]
[0xABED10] = new byte[n * 0x50];
for (每条记录) { 全零，但 +0x40 = -1、+0x4A = 1 }
```

`+0x40 = -1` 就是岸线平滑那套**掩码缓存的失效标记**，`+0x4A = 1` 是有效位——
两者的含义见 [stage-seeding-water.md](stage-seeding-water.md) §七。

然后**再扫一遍全图**，把每格的打包坐标写进对应记录的头 4 字节，
并把 `cell->+0x11B` 改成 `MapSeedClass->+0x30C`（= 4）：

```c
work[Y*side + X].packedXY = (Y << 16) | X;   // +0x00
cell->+0x11B = MapSeed->+0x30C;
```

于是工作记录布局又实锤两项：

| 偏移 | 含义 |
|------|------|
| `+0x00` | 打包坐标 `(Y<<16)\|X`（阶段 1 写入） |
| `+0x04` | **两处初始化代码都没碰它**，未确认 |
| `+0x38` | 陆块编号（`0x5A00C0/0090` 读写） |
| `+0x3C` | 入堆标记 |
| `+0x40` | 八邻域掩码缓存，`-1` = 失效 |
| `+0x4A` | 有效位，初值 1 |

## 六、`FUN_0042B1F0`：可用格子总数

MapType 1/2 的覆盖率分母，一行：

```c
int UsableCellCount() { return (H + 4) * W * 2; }   // W=[0x87F8DC], H=[0x87F8E0]
```

菱形地图的实际格数约 `2·W·H`，这里给高度多算了 4 行余量。
**不是**精确的"菱形内格子数"，复刻时别自作聪明换成精确值。

## 七、未确认

1. `MapSeedClass +0x310`（那个 25% 开关）具体开的是什么
2. 谁把 `MapSeedClass +0x180/+0x184` 装进传给 `0x5662B0` 的那个参数矩形——
   尺寸的**算法**已实锤（§三之二），落点 `MapClass+0xEC` 也已查实
   （[map-geometry.md](map-geometry.md) §五），只剩中间这一跳没跟到
3. `+0x30C` 恒为 4 是否在别处被改写
4. 工作记录 `+0x04`
