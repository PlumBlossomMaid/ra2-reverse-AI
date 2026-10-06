# 引擎的移动区子系统——阶段 6 的前置工程

> 状态：**整条链路已实锤并落 C++**（`code/rewrite/rmg/zones.{h,cpp}`，
> ctest `zones` 黄金对拍通过）。
> 取证：`memory/data/decomp/zones.txt`（`0x56C510` / `0x56CB90` / `0x56D230` /
> `0x5A1FB0`）、`memory/data/decomp/zones2.txt`（`0x56D6E0` / `0x56D5A0` /
> `0x56D460` / `0x581F50`），以及反汇编 `0x47D2B0-0x47DD61`、`0x483C80-0x483E28`、
> `0x544BE0`、`0x578460`、`0x56D3F0`。
>
> 这一块本身**一个随机数都不取**，但阶段 6 的区域划分、`want` 阈值全靠它，
> 不做出来阶段 6 就落不了地。

## 一、它在随机地图流水线里出现两次

```
0x599650  阶段 1 Init map
   ...
0x599EFD  call 0x56D6E0      ← 建「桥/隧道跨区连接表」（此刻全图还是空的）
0x599F07  call 0x56C510      ← 第一次洪泛
   ...
0x598E1F  阶段 5「Recalculating cell attributes」逐格 RecalcAttributes(-1)
   ...
0x594BA9  阶段 6 PlaceStartPoints 开场      call 0x56C510      ← 第二次洪泛
```

⚠️ **阶段 6 只调 `0x56C510`，不调 `0x56D6E0`。** 而 `0x56D6E0` 唯一一次跑是在
阶段 1、地图还是一片汪洋的时候，`MapClass+0x50` 那个 `DynamicVectorClass`
（桥/隧道连接表，数据 `+0x54`、个数 `+0x60`）当时必然是空的，之后
**再没有人往里加过东西**。于是阶段 4 建出来的桥（`0x58F2C0 BuildBridge`）
在阶段 6 眼里根本不存在，`0x56C510` 里那一整段「按连接表把两个区连起来」
是空转。复刻时可以直接把连接表当空的——但这属于「原版行为，不要修正」。

## 二、每格 4 字节的地形表 `MapClass+0x68`

| 字段 | 含义 | 谁写 |
|------|------|------|
| `byte[0]` | **地面类别**，取自 `CellClass +0x4C`，取值 0..7，见 §四 | `CellClass::RecalcAttributes` |
| `byte[1]` | **层高**，取自 `CellClass +0x11B` | 同上 |
| `word[2]` | **连通区编号**，洪泛时填，`0x56C510` 开场清 0 | `0x56C510` / `0x56CB90` |

- 表头指针 `MapClass+0x68`，表长（格数）`MapClass+0x6C`。
- 下标换算 `MapClass::CellToIndex`（`0x56D3F0`）：

```c
idx = Y * (MapClass->+0xF8 + MapClass->+0xF4 + 1) + X;
if (idx < 0)              idx = 0;          // ⚠️ 负数夹到 0，不是夹到末尾
else if (idx >= +0x6C)    idx = +0x6C - 1;
```

`0x56C510` / `0x56D230` / `0x56D460` / `0x56D5A0` 里都把这段展开抄了一遍，
夹取行为完全一致。

顺带一提，`MapClass+0x70` 还有个 10 字节/格的表，`RecalcAttributes` 往它的
`+8` 写同一个层高；随机地图不读它。

## 三、⚠️ 纠正：阶段 5 的 `RecalcAttributes` 那一遍**就是**为了填这张表

[stage-recalc.md](stage-recalc.md) 原先只把 `CellClass::RecalcAttributes`
（`0x47D2B0`）当成一遍「校验/清理」，因为它对 `+0x38 / +0x11A / +0x11B` 只有
四条写。这个说法**不完整**：它每格的收尾无条件干三件事——

```
0047d54a  call 0x47ca80              ; 重算 +0xEC 相关的杂项
0047d551  call 0x483c80              ; ← CellClass::RecalcLandType，算 +0x4C
0047d560  mov  [ebx+1], dl           ; zoneTable[idx].byte1 = cell->+0x11B（层高）
0047d569  mov  [ecx+8], al           ; MapClass+0x70 那张表也写一份层高
0047d571  mov  [ebx],   dl           ; zoneTable[idx].byte0 = cell->+0x4C（地面类别）
```

`ebx` 是函数开头算好的 `zoneTable + idx*4`：

```
0047d2b8  cmp  esi, 0xabdc50         ; ⚠️ 共享哑元直接跳到结尾，一个字节都不写
0047d2bf  je   0x47dd5a
0047d2c5  lea  edi, [esi+0x24]       ; &cell->MapCoords
0047d2ce  call 0x56d3f0              ; idx = CellToIndex(coord)
0047d2d3  mov  ecx, [0x87f850]       ; = MapClass::Instance + 0x68
0047d2da  lea  ebx, [ecx+eax*4]
```

函数有三个出口（`0x47D571` / `0x47D7F2` / `0x47DD4B`），**三个都写**这两个字节。
所以：阶段 5 那一遍「Recalculating cell attributes」的真正产物，是把整张
地形表刷成当前地形的样子，好让阶段 6 的洪泛有东西可读。

`[0x87F850]` 只有 `0x47D2D5` 这一处引用，这条链是唯一的。

## 四、`CellClass::RecalcLandType`（`0x483C80`）——八个地面类别

```c
if (!MapClass::IsWithinUsableArea(&cell->coord, true))   // 0x578460
    { cell->+0x4C = 7; return; }                         // 出图
if (cell->+0x44 != -1) {                                 // 挂着 overlay
    OverlayTypeClass* o = OverlayTypes[cell->+0x44];
    if (o->+0x22D)                 { +0x4C = 1; return; }
    if (o->+0x2A8)                 { +0x4C = 2; return; }
    if (Ground[o->+0x298].Foot == 0.0f) { +0x4C = 6; return; }
    if (o->+0x2B5)                 { +0x4C = 6; return; }
    if (o->+0x2B4)                 { +0x4C = 0; return; }
}
switch (cell->+0xEC) {                                   // 地面类型（GroundType）
    case 2:  +0x4C = 4; return;                          // Water
    case 6:  +0x4C = 3; return;                          // Beach
}
if (Ground[cell->+0xEC].Foot <= 0.01) { +0x4C = 6; return; }   // 走不了
for (obj = cell->+0xE4; obj; obj = obj->+0x30) { ... +0x4C = 5 ... }  // 矿
+0x4C = 0;                                               // 其余都算「空地」
```

阈值 `0.01` 是内联常量 `[0x7E3808]`（`3F847AE147AE147B`）。

**随机地图跑到阶段 6 时，格子上既没有 overlay（`+0x44` 还是阶段 1 写的 −1，
第一个写它的是阶段 8 的矿），也没有任何 object（`+0xE4 == 0`）。**
于是只剩四条路能走通，类别取值只可能是：

| `+0x4C` | 什么时候 | 随机地图里对应什么 |
|---------|---------|-------------------|
| **0** | 其余 | 草地 / 空地 / 坡道 / 桥面 |
| **3** | `+0xEC == 6`（Beach） | 岸砖里的沙滩子格 |
| **4** | `+0xEC == 2`（Water） | 水面 |
| **6** | `Ground[+0xEC].Foot <= 0.01` | 悬崖（Rock）、Wall |
| **7** | 出了 `MapClass` 的可用区矩形 | 菱形外圈 |

类别 1 / 2 / 5 需要 overlay 才可能出现，阶段 6 之前一个都不会有。

### `0x578460` = 用 `MapClass` 自己那份矩形的 `IsWithinUsableArea`

和阶段 6 用的 `0x578640` 是同一段展开，只是矩形取自
`MapClass+0xFC/+0x100/+0x104/+0x108` 而不是调用方给的：

```
W = MapClass->+0xF4; adj = 0
if (flag) { cell = TryGetCellAt(c);                    // ⚠️ 先取格再判界，
            adj = (int8)cell->+0x11B;                  //    出图会写共享哑元 +0x24
            if (cell->+0x11C != 0 && x+y < W + r.y*2 + 4 + adj) ++adj; }
if (x+y <= W + r.y*2 + adj)             return false;
if (x+y >  W + (r.y+r.h)*2 + 2 + adj)   return false;
if (x-y >= (r.x+r.w)*2 - W)             return false;
if (y-x >= W - r.x*2)                   return false;
return true;
```

`0x483C80` 传的 `flag` 是 **1**，所以每个出图格子都会往 `[0xABDC74]` 写一次坐标。

### 那个矩形到底是多少：`(2, 5, W, H)`——已实锤

`+0xFC..+0x108` 只有三处写入点，全在 `MapClass::Set_LocalSize`（`0x567230`）里：
先与 `+0xEC` 那个整图矩形求交（`0x421B60`），再逐字段夹取

```
+0xFC  = max(x, 2)                      ; 0056726E-00567292
+0x100 = max(y, 2)
+0x104 = min(w, (+0xF4 - x) - 2)        ; 0056728C-005672AF
+0x108 = min(h, (+0xF8 - y) - 6)        ; 005672B5-005672CD
```

而 `0x567230` 唯一的调用者是 `0x654490`（`Set_LocalSize` 的外壳），
它在随机地图流水线里被调到的第一处就在**阶段 1 自己**：

```asm
00599739  push 1 / push ebx(=0)
0059993c  lea  eax, [esp+0x44]        ; ← 加上刚才 8 字节 push，指向栈上的 Size 矩形
00599d48  call 0x653f50               ; -> 0x565C10 MapClass::Set_Dimensions
00599d4d  lea  ecx, [esp+0x2c]        ; ← LocalSize 矩形
00599d57  call 0x654490               ; -> 0x567230 MapClass::Set_LocalSize
```

两个矩形在阶段 1 开头就摆好了，和写进内存 INI 的 `[Map] Size` / `[Map] LocalSize`
是**同一份栈变量**（`stage-init.md` §三）：

```asm
0059974e  mov [esp+0x3c], ebx(=0)     ; Size.X = 0
00599752  mov [esp+0x40], ebx(=0)     ; Size.Y = 0
0059973f  mov [esp+0x44], edx         ; Size.W = W + 4
00599756  mov [esp+0x48], edx         ; Size.H = H + 12
0059975a  mov [esp+0x2c], 2           ; LocalSize.X = 2
00599762  mov [esp+0x30], 5           ; LocalSize.Y = 5
0059972f  mov [esp+0x34], ecx         ; LocalSize.W = W    (MapSeed +0x180)
0059976a  mov [esp+0x38], eax         ; LocalSize.H = H    (MapSeed +0x184)
```

`Set_Dimensions`（`0x5662B0` 那段）把 X/Y 强行清零，于是

```
MapClass +0xEC..+0xF8  = (0, 0, W+4, H+12)      ← 整图矩形，Wm/Hm
MapClass +0xFC..+0x108 = (2, 5, W,   H)         ← 可用区矩形（求交与夹取都不改动它）
```

夹取那一步逐条验算：`max(2,2)=2`；`max(5,2)=5`；
`min(W, (W+4)-2-2) = W`；`min(H, (H+12)-5-6) = min(H, H+1) = H`。**四条全部原样通过**，
所以可用区矩形就是 `(2, 5, W, H)`，用整图尺寸表示是 `(2, 5, Wm-4, Hm-12)`。

代进 `0x578460` 的四条不等式（`level` = 该格 `+0x11B`，坡道格再 +1）：

```
X + Y >  Wm + 10 + level
X + Y <= Wm + 2*Hm - 12 + level
X - Y <  Wm - 4
Y - X <  Wm - 4
```

与 `MapGrid::InMap` 的菱形（`X+Y > Wm`、`X+Y <= Wm+2Hm`、`|X-Y| < Wm`）一比就是
**从菱形四边各往里缩一圈**：上边缩 5 行、下边缩 6 行、左右各缩 2 列。
菱形最外那一圈格子的地面类别因此恒为 **7**（出图），洪泛完全不碰它们。

> 这条同时关掉了 [stage-init.md](stage-init.md) §七 与
> [map-geometry.md](map-geometry.md) §六 里那条「谁把 `MapSeedClass +0x180/+0x184`
> 装进传给 `0x5662B0` 的参数矩形」——**就是阶段 1 自己**，
> `0x599D48` 传的正是它刚拼好的 `{0, 0, W+4, H+12}`。
> `MapClass::Read_INI`（`0x687470` 读 `Size`、`0x687503` 读 `LocalSize`）
> 是同一对 setter 的另一个调用者，走的是存盘地图的路径，两条路结果一致。

## 五、`CellClass +0xEC` = GroundType，来自 `.tmp` 子块头的 TerrainType

阶段 6 之前，`+0xEC` 的唯一来源是 `0x47D89A-0x47D8AA`：

```
0047d89a  movzx eax, byte [esi+0x11a]   ; 子块号
0047d8a3  mov   ecx, ebp                ; TileTypeClass*
0047d8a5  call  0x544be0
0047d8aa  mov   [esi+0xec], eax
```

`0x544BE0`：

```c
img = tile->vtable_0x9C();          // .tmp 图像头，没装载就是 0 → 返回 0
if (!img) return 0;
idx  = subtile % (img->w * img->h); // idiv 取余，不是边界判定
blk  = img->cells[idx];
if (!blk) return 0;                 // 空洞
return TerrainToGround[(int8)blk[0x29]];   // 表在 0x8288E4
```

`blk + 0x29` 就是 TS/RA2 `.tmp` 子块头里的 **TerrainType** 字节
（`+0x28` 高度、`+0x29` 地形、`+0x2A` 坡道——`+0x2A` 我们在
[stage-recalc.md](stage-recalc.md) 里已经用过了）。

### `TerrainToGround`（`0x8288E4`，16 个 dword）

| `.tmp` TerrainType | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GroundType | 0 | 8 | 8 | 8 | 8 | 10 | 9 | 3 | 3 | 2 | 6 | 1 | 1 | 0 | 7 | 3 |

### GroundType 枚举与 `Ground[]` 数组

名字表在 `0x81DA28`（12 项），就是 rules.ini 里那 12 个小节：

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|----|----|----|
| Clear | Road | Water | Rock | Wall | Tiberium | Beach | Rough | Ice | Railroad | Tunnel | Weeds |

于是上面那张映射表读起来完全对得上 `.tmp` 的官方语义：
`1..4 → Ice`、`5 → Tunnel`、`6 → Railroad`、`7/8/15 → Rock`、`9 → Water`、
`10 → Beach`、`11/12 → Road`、`0/13 → Clear`、`14 → Rough`。

### 真数据长什么样（`memory/data/rmg/tile_geometry.txt`）

`code/randmap/dump_tile_geometry.py` 现在把 `+0x29` 一起导出来了，覆盖阶段 6 之前
地图上**可能出现的全部四组砖**：`ClearTile` / `GreenTile` / `WaterSet` / `ShorePieces`。
把 TerrainType 过一遍 `TerrainToGround` 再过 §四那张表，结果是：

| 组 | 出现的 TerrainType | → GroundType | → 地面类别 |
|---|---|---|---|
| `clear`（1 块） | 13 | Clear | 0 |
| `green`（1 块） | 14 | Rough | 0 |
| `shore`（42 块） | 10 / 14 / 9 | Beach / Rough / Water | **3** / 0 / **4** |
| `water`（14 块） | 9 / **15** | Water / **Rock** | **4** / **6** |

两条会咬人的事实：

**其一：水砖里有石头。** `water07` / `water08`（都是 2×2）与 `water14`（1×1）
整块都是 TerrainType 15 → Rock → `Foot=0%` → 地面类别 **6（走不通）**。
按 [stage-water-refine.md] 那套 `shape = n/40` 的挑砖规则，
`water07/08` 落在大水面的 8 个候选里、`water14` 落在小浪花的 6 个候选里，
所以**任何一张随机地图的水域里都会散着一堆不可通行的礁石格**。
洪泛看到它们会把水面切开，这是原版行为，复刻时不许"清理"掉。

**其二：温和与雪地的 TerrainType 不一样。** 56 块砖的**几何**
（`w`/`h`/`present`/`height`/`ramp`）逐块逐子块完全相同，但 `shore41`（6×4）与
`shore42`（9×5）这两块大岸砖的 TerrainType 两个气候不同——雪地把温和的一大片
沙滩/水换成了 Rough：

```
shore41.tem                    shore41.sno
 -- 14 14 14 -- --              -- 14 14 14 -- --
 14 10 10 10 10 10              14 14 14 10 10 10
 10 10 10 10  9  9              14 14 10 10 10  9
 10  9  9  9  9  9              14 10  9  9  9  9
```

于是"取一份温和的剖面两个气候通用"这条捷径**对读 terrain 的地方不成立**，
必须按气候取（`tile_profiles.real_spec(group, k, theater)`）。
贴砖器/岸线那几份只用 `w`/`h`/`present`/`height`/`ramp` 的基准仍然通用。

`Ground[]` 数组在 `0x89EA48`，**36 字节一项**，字段顺序由装载函数
`0x674030` 附近的读键顺序钉死：

| 偏移 | 键名 |
|------|------|
| `+0x00` | `Foot` |
| `+0x04` | `Track` |
| `+0x08` | `Wheel` |
| `+0x0C` | `Hover` |
| `+0x10` | （`Winged`，硬写 `1.0f`，`0x674148`） |
| `+0x14` | `Float` |
| `+0x18` | `FloatBeach` |
| `+0x1C` | （疑似 `Amphibious`，未确认） |
| `+0x20` | `Buildable` |

`0x483C80` 读的是 `+0x00`，也就是 **`Foot`**。照原版 `rulesmd.ini`，
只有 `[Rock]` 与 `[Wall]` 的 `Foot=0%`，其余 10 个小节都 ≥ 50%——
所以「`Foot <= 0.01` ⇒ 类别 6」在随机地图里就等价于「悬崖砖」。

## 六、`MapClass::Update_Pathfinding_1`（`0x56C510`）

Ghidra 给的符号名，实际干的事是「重建整套连通区 + 每种移动区的区类型表」。
返回值是**最大的那个区的编号**。

```c
uint Update_Pathfinding_1(MapClass* m)
{
    // ① 把 16×16 = 256 个「跨区连接桶」清空（每桶 0x18 字节，vtable+0xC）
    for (i = 0; i < (**m->+0x14).Count; ++i) buckets[i].Clear();

    // ② 释放 +0x18..+0x48 那 13 个「区编号 → 区类型」数组
    for (i = 0; i < 13; ++i) { free(m->zoneType[i]); m->zoneType[i] = 0; }

    // ③ 全表的区编号清 0
    for (p = m->+0x68; p < m->+0x68 + m->+0x6C*4; p += 4) *(u16*)(p+2) = 0;

    // ④ 线性扫全表洪泛，区编号从 1 开始
    landOf.push(7);                      // ⚠️ 0 号槽占位，填的是「出图」类别
    zone = 1;  best = -1;  biggest = -1;
    for (p = m->+0x68; p < end; ) {
        if (p[0] == 7 || *(u16*)(p+2) != 0) { p += 4; continue; }
        [0xABDE8C] = 0;                  // 跨区连接去重用的「上一个邻区」缓存
        n = Flood(p, zone, &runLen);     // 0x56CB90，返回本区格数
        if (n > biggest) { biggest = n; best = zone; }
        landOf.push(p[0]);
        ++zone;
        p += runLen * 4;                 // ⚠️ 跳过刚才那一「行段」，不是 +4
                                         //    runLen 可以是 0 甚至 -1，见 §七
    }
    m->+0x4C = zone;                     // 区个数（含 0 号占位槽）

    // ⑤ 桥/隧道连接表 → 跨区连接桶（随机地图里 m->+0x60 == 0，整段空转）
    ...

    // ⑥ 桶 → CSR 邻接表：先数度数，再按区分配 u16 数组填邻居
    ...

    // ⑦ 13 种移动区，各生成一份「区编号 → 区类型」表
    for (mz = 0; mz < 13; ++mz) {
        u16* t = new u16[m->+0x4C];
        for (z = 0; z < m->+0x4C; ++z)
            t[z] = (MoveTable[mz][landOf[z]] != 1);      // 走得通 → 0，走不通 → 1
        next = 2;
        for (z = 0; z < m->+0x4C; ++z) {
            if (t[z] != 0) continue;
            key = MoveTable[mz][landOf[z]];
            t[z] = next;  stack = {z};
            while (!stack.empty()) {                      // 沿邻接表 DFS
                u = stack.pop();
                for (v : adj[u] 倒序)                     // ⚠️ 邻居从后往前扫
                    if (MoveTable[mz][landOf[v]] == key && t[v] == 0)
                        { t[v] = next; stack.push(v); }
            }
            ++next;
        }
        t[0] = 0xFFFF;                                    // 0 号占位槽
        m->zoneType[mz] = t;
    }
    return best;
}
```

第 ⑦ 步的效果：**走不通的区一律得到区类型 1**；走得通的区按「桥/隧道连接」
合并成若干「超级区」，类型编号从 2 起。随机地图里连接表是空的，
所以每个走得通的连通区都自成一个类型，编号就是它在扫描序里的名次 + 1。

### `MoveTable`（`0x82A594`，13 行 × 8 列 dword，行距 0x20，终点 `0x82A734`）

行 = MovementZone，列 = §四那八个地面类别。`1` 表示走得通。

| MZ＼类别 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----------|---|---|---|---|---|---|---|---|
| 0 | **1** | 2 | 2 | 2 | 2 | 2 | 2 | 3 |
| 1 | 1 | 1 | 2 | 2 | 2 | 2 | 2 | 3 |
| 2 | 1 | 1 | 1 | 2 | 2 | 2 | 2 | 3 |
| 3 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 3 |
| 4 | 1 | 1 | 2 | 1 | 1 | 2 | 2 | 3 |
| **5** | **1** | 2 | 2 | **1** | **1** | 2 | 2 | 3 |
| 6 | 1 | 1 | 1 | 2 | 2 | 2 | 1 | 3 |
| 7 | 1 | 2 | 2 | 2 | 2 | 1 | 2 | 3 |
| 8 | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 3 |
| 9 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 3 |
| 10 | 2 | 2 | 2 | 2 | 1 | 2 | 2 | 3 |
| 11 | 2 | 2 | 2 | 1 | 1 | 2 | 2 | 3 |
| 12 | 1 | 1 | 1 | 2 | 2 | 2 | 2 | 3 |

行 10 只认类别 4（水）——那就是舰船；行 5 认 0/3/4（陆 + 沙滩 + 水）——两栖；
行 9 除了出图什么都能过。语义与 `MovementZone` 枚举吻合，但**具体的枚举名
与行号的对应关系尚未逐个坐实**，随机地图也只用到第 0 行与第 5 行：

```
00594bae  mov ecx, [0xabe014]              ; MapType
00594bb6  mov ebx, [esp+ecx*4+0x38]        ; 栈上五项表 {5,5,5,0,0}
00594bba  mov edx, [ebx*4+0x87f800]        ; = MapClass+0x18 + zone*4
00594bc1  mov bp,  [edx+eax*2]             ; want = zoneType[largestZone]
```

- `MapType ∈ {0,1,2}` → 移动区 **5**：陆、沙滩、水连成一片，只有悬崖切断；
- `MapType ∈ {3,4}` → 移动区 **0**：只有类别 0 走得通，水和沙滩都切断。

## 七、`0x56CB90`——行段洪泛（scanline flood fill）

一次处理「同一行里连续的一段」，然后对上下两行递归。参数
`(entry*, zoneId, int* runLen)`，返回本次涂到的格数。

下面改用**下标形式**写（`st = side`，`T[i]` 是第 i 格），因为复刻时要照抄的
恰恰是那些"指针越界一格"的细节：

```c
land = T[p].land;  isRock = (land == 6);   // ⚠️ 类别 6 在跨区连接判定里有豁免
h = T[p].level;
// ── 向左（低下标）扩 ──
L = p;
do { if (abs(T[L].level - h) > 1) break;   // 0x56CBCA  cmp eax,2 / jge
     T[L].zone = zone;  h = T[L].level;  --L; } while (T[L].land == land);
登记(T[L].zone,  abs(T[L].level - h) < 2 || isRock);
// ── 向右（高下标）扩 ⚠️ 沿用左扫留下的 h（= 最左那格的层高）──
R = p;
while (T[R].land == land && abs(T[R].level - h) < 4) {   // 0x56CCE2 cmp eax,4/jge
     T[R].zone = zone;  h = T[R].level;  ++R; }
登记(T[R].zone,  abs(T[R].level - h) < 2 || isRock);
painted  = (R - L) - 1;
*runLen  = (R - p) - 1;                    // ⚠️ 可能是 0，也可能是 -1
// ── 上下两行：各扫闭区间 [L, R]，比涂到的段左右各多一格 ──
upper = L - st;   upper_end = R - st - 1;
lower = L + st;   lower_end = R + st;
for (;;) {
  if (upper > upper_end + 1) {             // 0x56CE4D  pcVar13 + 4 < pcVar18
      while (lower <= lower_end) { ...下行同构... }
      return painted;
  }
  ref = (upper <  upper_end) ? upper + st + 1
      : (upper == upper_end) ? upper + st
                             : upper + st - 1;
  if (T[upper].zone != 0)                        { 登记(...); ++upper; }
  else if (T[upper].land != land ||
           abs(T[upper].level - T[ref].level) > 1) { ++upper; }
  else { painted += Flood(upper, zone, &sub); upper += sub; }
}
// 下行的 ref 是镜像的：(lower <  lower_end-1) ? lower-st+1
//                    : (lower == lower_end-1) ? lower-st : lower-st-1
```

`ref` 那三分支就是「拿正下方 / 斜下方那一格的层高来比」——因为上行游标扫的是
比本段更宽一格的闭区间，两个端点得往回错一格才对得上本段的格子。

### ⚠️ `*runLen` 可以是 0，甚至是 -1

外层 `0x56C510` 的扫描游标是 `p += runLen` 而**不是** `p = R`。
`runLen = (R - p) - 1`，于是：

* 右扫只涂了 `p` 一格时 `runLen == 0`，游标原地不动——下一轮 `p` 已被涂过，
  走「已有区编号」那条分支才 `++p`，所以不会死循环；
* 右扫一格都没成时（`abs(level(p) - 最左那格的 level) >= 4`）`runLen == -1`，
  **游标往回退一格**。

两种都真的会发生：拿 `memory/data/rmg/zones_golden.txt` 那 14 个用例跑一遍，
`runLen == 0` 出现 505 次、`runLen == -1` 出现 5 次。照抄，不要"修"成 `p = R`。

### ⚠️ 向左的层高门限是 `<= 1`，向右却是 `<= 3`

`0x56CBCA` 是 `cmp eax, 2 / jge`，`0x56CCE2` 是 `cmp eax, 4 / jge`——同一个判据
写了两个常量。左右不对称，这是 25 年前的代码本来的样子，**照抄**。
（后面处理上下两行时用的又是 `< 2`，和向左那份一致。）

还有第二处不对称容易看漏：**两个方向的 `h` 是同一个变量**。左扫结束时
`h` 停在「最左那格」的层高，右扫拿着它当起点再逐格更新。所以右扫的第一步
比的是 `abs(level(p) - level(最左那格))`——一段长坡上这个差可以轻松超过 4，
这正是上面 `runLen == -1` 的来历。把这里当成"和左扫对称"来写，
结果会在有坡的图上分岔。

段的两端还会各做一次「隔壁是别的区就登记一条跨区连接」的动作，判据是
`邻区编号 != 0 && != 本区 && != [0xABDE8C] && (层高差 < 2 || isRock)`；
连接对打包成 `(邻区 << 16) | 本区`，进桶 `((邻区 & 0xF) << 4) | (本区 & 0xF)`，
桶内线性去重。全函数一共 **4 处登记点**（左端、右端、上下两行递归各一处）。

### ⚠️ 这些桶**确实**喂给了第 ⑥/⑦ 步（原「未确认 #3」已关闭）

第 ⑤ 步（桥/隧道表 → 桶）在随机地图里是空转没错，但第 ⑥ 步读的**不是**第 ⑤ 步
的产物，而是整个桶数组：

```c
for (off = 0; off < 0x1800; off += 0x18) {        // 256 桶 × 0x18 字节
    bucket = **(this+0x14) + off;
    for (k = 0; k < bucket.Count; ++k) { ++deg[pair & 0xFFFF]; ++deg[pair >> 16]; }
}
```

`0x1800 / 0x18 = 256`，正是第 ① 步清空的那 256 个桶，也正是 `0x56CB90`
往里塞东西的那 256 个桶（两边都是 `**(this+0x14) + idx*0x18`）。
**所以洪泛登记的跨区连接是第 ⑦ 步 DFS 唯一的边来源。**

这条的后果不小：洪泛只把「地面类别相同」的格子归进同一个区，于是陆、沙滩、水
天生就是三批不同的区；但第 ⑦ 步在移动区 5（两栖，类别 0/3/4 都判 1）下
会沿着这些边把**相邻的陆区 + 沙滩区 + 水区合并成同一个区类型**。
阶段 6 用 `MapType ∈ {0,1,2}` → 移动区 5 去取「最大区的区类型」，
拿到的是这个合并后的超级区，而不是单独一块陆地。移动区 0 则相反：
沙滩/水在表里是 2，`t[z]` 直接被置 1，DFS 的 `key == 1` 过不去，不会合并。

顺带两条记账细节，复刻时容易漏：

* 第 ⑤ 步打包前做了 `min/max` 排序（`uVar13 = min, uVar10 = max`），
  `0x56CB90` **没有**——它固定按 `(邻区 << 16) | 本区` 打包。于是同一对区
  从两个方向各发现一次时会落进两个不同的桶、存成两个不同的 dword，
  去重去不掉。度数因此多算，CSR 数组也更长。不影响 DFS 的结果，但影响
  内存布局，照抄即可。
* `[0xABDE8C]`（上一个邻区缓存）只在登记条件**全部通过**时才更新成本次的邻区；
  条件没过就保持原值。它由 `0x56C510` 在每次调 `0x56CB90` 前清 0。

上下两行的推进用 `stride = m->+0xF8 + m->+0xF4 + 1`，`p ± stride*4`，
边界靠「本段左右端点各外扩一格」的指针比较，没有显式的 X 范围检查——
所以它会读到相邻行的格子。这也是原版行为。

## 八、`MapClass::GetMovementZoneType`（`0x56D230`）

```c
u16 GetMovementZoneType(CellStruct* c, int mz, bool followBridge)
{
    CellStruct* q = c;
    if (followBridge) { ... 桥面则顺着桥走到落脚点 ... }   // 随机地图恒传 0
    idx = clamp(CellToIndex(q));
    return m->zoneType[mz][ *(u16*)(m->+0x68 + idx*4 + 2) ];
}
```

阶段 6 的 `GetMovementZoneType(coord, zone, 0)` 走的是短路径，
就是「查这一格的区编号，再查这个区在该移动区下的类型」。

## 九、两个「一格变了」的增量更新（随机地图不走）

`MapClass::ResetZones`（`0x56D460`）与 `MapClass::RecalculateZones`（`0x56D5A0`）
是同一套骨架：看这一格的 8 个邻格，如果**邻区类型互不相同的次数 < 4**，
就把自己的区编号直接抄邻格的；否则退化成整张图重来（调 `0x56C510`）。
随机地图流水线里一次都没调到，记在这里是为了说明 `+0x68` 表在别处也会被改。

## 十、`0x5A1FB0`——不是校验，是「从每个出生点往外泼漆」

阶段 6 尾巴上那个函数**无条件 `return 1`**，所以主流程里那圈重试
不可能由它触发。它真正干的是：对每个出生点（`MapSeedClass+0x50` = 玩家数）

1. 先把所有工作记录的 `+0x3C` 清 0（走 `MapClass` 格子迭代器）；
2. 以出生点为源做一次**优先队列洪泛**（堆容量 800，节点池 `0x1900` 字节），
   代价由 `0x4CAC40` 算；
3. 每弹出一个格子就把工作记录的 `+0x45` 置 1、`+0x3C` 置成「第几个出生点」，
   再把 8 个邻格里「在菱形内、`+0x3C == 0`、且 `0x486380` 判为未定砖」的推进队列；
4. **最多扩 400 次**（`local_3c < 400`）就收工。

于是 `+0x45` 与 `+0x3C` 的语义也顺带定下来了：`+0x3C` 在这里被复用成
「离哪个出生点最近」，`+0x45` 是「被出生点辐射到过」。

## 十一、复刻状态：✅ 已落地并对拍

`code/rewrite/rmg/zones.{h,cpp}`，回归 `test/test_zones.cpp`（ctest 名 `zones`）。
落地的东西：

| 重写侧 | 对应原版 |
|---|---|
| `kTerrainToGround[16]` | `0x8288E4` |
| `kMoveTable[13][8]` | `0x82A594`（行距 0x20，终点 `0x82A734`） |
| `RecalcLandType` | `0x483C80` 的随机地图子集（无 overlay、无 object） |
| `ClampLocalRect` | `0x421B60` 求交 + `0x567230` 逐字段夹取 |
| `IsWithinUsableArea` | `0x578460` |
| `ZoneTable::Flood` | `0x56CB90` |
| `ZoneTable::Rebuild` | `0x56C510` 的第 ①③④⑥⑦ 步 |
| `ZoneTable::GetMovementZoneType` | `0x56D230` 的短路径 |

黄金基准由 `code/randmap/dump_zones_golden.py` 用 Unicorn 跑**原版机器码**取得
（存档 `memory/data/rmg/zones_golden.txt`，14 个用例、最大 41×41 表、
最多 220 个区）。它绕开整条地形流水线：直接往 `MapClass+0x68` 里灌合成的
`(地面类别, 层高)`，把 256 个跨区连接桶按 `0x565840-0x5658DD` 的原样摆好，
再 `__fastcall` 调 `0x56C510`，最后逐格比区编号、比 13 张区类型表、比返回值。
整条路径无 x87 无随机数，纯整数，Unicorn 在这里完全可信。

对拍的"有牙齿"验证做了两次：把右扫门限从 4 改回 2，第 119 格当场对不上；
`runLen <= 0` 的两种情形也确认真的被基准覆盖到（505 次 0、5 次 -1）。

一处**观测不到**的细节照实记下：第 ⑦ 步 DFS 扫邻居是**倒着**扫的
（`0x56CA..` 的 `puVar22 = puVar22 + -1`），但同一连通分量里所有点拿到的编号
都一样，遍历顺序不影响任何输出。重写按原样倒着写，属于形似而非必需。

还需要由调用方注入的外部数据只有一项：**`Ground[].Foot`**（12 个小节，
`rulesmd.ini`），`0x483C80` 只读这一列（步长 36 字节），且只用来判 `<= 0.01`
（比的是 **double** 常量 `0x7E3808`，不是 float）。

第 ② 步（释放旧的 13 张表）与第 ⑤ 步（桥/隧道两端连通登记）没有实现：
前者在 C++ 里由 `ZoneResult` 自己持有，后者在随机地图里 `MapClass+0x60 == 0`
整段空转。两条都在代码注释里标了出处。

## 十二、未确认

1. 13 行 `MoveTable` 与 `MovementZone` 枚举名的逐行对应（只坐实了
   「行 10 = 只走水」「行 5 = 陆/沙滩/水」两条，随机地图也只用得到 0 和 5）
2. `Ground[] + 0x1C` 是不是 `Amphibious`（读键顺序里没看到它，可能在别处补写）
3. ~~`0x56CB90` 建出来的跨区连接桶，在随机地图里到底有没有改变过第 ⑦ 步的结果~~
   ✅ **已关闭**：会。第 ⑥ 步读的就是那 256 个桶（`0x1800 / 0x18`），
   洪泛登记的边是第 ⑦ 步 DFS 唯一的边来源；移动区 5 下陆/沙滩/水会被合并成
   一个区类型。详见 §七。
4. `OverlayTypeClass` 的 `+0x22D` / `+0x298` / `+0x2A8` / `+0x2B4` / `+0x2B5`
   各是什么键（随机地图跑到阶段 6 时一定没有 overlay，所以不影响复刻）
5. `0x47CA80`（`RecalcAttributes` 收尾前调的另一个）到底改了什么
6. `MapClass+0x70` 那张 10 字节/格的表是干什么的
