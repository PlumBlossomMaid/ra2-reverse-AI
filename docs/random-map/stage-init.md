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
3. 地图尺寸相关的派生量

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
| `Map` | `Size` / `LocalSize` | 由 `local_7c` / `local_8c={2,5,…}` 拼出的矩形 |
| `Map` | `Level` | `MapSeedClass +0x30C`，此处被写死为 **4** |
| `Basic` | `Player` | 取自 `[0xA83C9C]->+0x24` |
| `Lighting` | `Ambient` / `IonAmbient` | `预设表[Time] × [0x7E3808]`（`Time` = 参数 `+0x48`） |
| `Lighting` | `RedTint`/`GreenTint`/`BlueTint` | 均 `1.0` |
| `Lighting` | `IonRed`/`IonGreen`/`IonBlue` | `0.3` / `0.4` / `0.75` |
| `Lighting` | `Ground` / `IonGround` / `IonLevel` | `0` |
| `Lighting` | `Level` | `预设表[Time] / 100`（整数除） |

`Theater == 0` 与否会切换取哪一组预设表指针（`+0x1A8` vs `+0x1C4`，共 4 组 8 个指针），
即**两套 theater 各一份光照预设**，再按 `Time` 下标取值。这些表本身在 `.bss`，内容未确认。

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
2. `Size` / `LocalSize` 两个矩形的具体来源（`local_7c` / `local_8c` 的填法）
3. `+0x30C` 恒为 4 是否在别处被改写
4. 工作记录 `+0x04`
