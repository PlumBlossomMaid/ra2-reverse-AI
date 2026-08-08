# 阶段 7：Adding tech buildings——撒中立科技建筑

> 状态：**两条分支、6 个取数点、rules.ini 键名全部实锤，且已落 C++ 并逐位对拍**（见 §八）。
> 取证：`memory/data/decomp/rmg_tech_buildings.txt`、`rmg_tech_helpers.txt`。

## 一、门禁

```c
FUN_004068e0("RMG: Adding tech buildings");
if (seed->MapType != 0)          // +0x3C
    AddTechBuildings(seed);      // 0x5A95B0
```

`MapType == 0` 整段跳过。函数内部再按 `MapType == 2` 分成两条完全不同的路。

## 二、分支 A：`MapType == 2`——给每个有出生点的区域各补几栋

```c
// 取数点 0x5A95BF：PickBelow(3)，比例常数 0x7ED8C0 = 3/(2^32-1)
int n; do { n = (int)((double)(uint32)Random() * (3.0/4294967295.0)); } while (n > 2);

for (; n > 0; --n)                                   // n == 0 时什么都不做
    for (int i = 0; i < regionCount; ++i)            // [0xABDFA0]
        if (regions[i]->startCount > 0)              // +0x20，阶段 6 分的配额
            PlaceRandomBuilding(regions[i]);         // 0x595400，每次 2 个随机数
```

- 外层 `n` 轮，**每轮把所有有出生点的区域都刷一遍**，所以总放置次数是
  `n × (有出生点的区域数)`
- `regions` 数组与个数在每次 `0x595400` 之后**重新从全局读**
  （`0x5A9615`/`0x5A961B`）——因为放建筑可能会动那个向量
- 这条分支到此 `return`，不走下面的逻辑

## 三、分支 B：`MapType != 0 && != 2`——全图随机撒

### 找业主

```c
int idx  = HouseTypeClass::FindIndexByName("Neutral");   // 0x5117D0，字符串 0x82BA08
HouseClass* owner = FindHouseByType(idx);                // 0x502D30
```

`0x502D30` 遍历 `[0xA8022C]`（HouseClass 数组，个数 `[0xA80238]`），
找 `[[house+0x34]+0xB8] == idx` 的那一家。所以科技建筑属于 **Neutral 阵营**。

### 撒几栋

```c
// 取数点 0x5A9645：PickBelow(5)，比例常数 0x7EDAB0 = 5/(2^32-1)
int m; do { m = (int)((double)(uint32)Random() * (5.0/4294967295.0)); } while (m > 4);
```

`m ∈ [0,4]`，`m == 0` 时**这一整段一个建筑都不放，但那个随机数已经花了**。

### 每栋的放置流程

```c
for (; m > 0; --m) {
    bool placed = false;
    int attempt = 0;
    void* mem = operator new(0x720);                 // BuildingClass 大小 0x720
    BuildingClass* b = NULL;
    if (mem) {
        // 取数点 0x5A96B8：PickBelow(rules->NeutralTechBuildings.Count)
        int cnt = [[0x8871E0] + 0xAEC];
        int k; do { k = PickBelow_inline(cnt); } while (k > cnt - 1);
        b = new(mem) BuildingClass([[0x8871E0] + 0xAE0][k], owner);   // 0x43B740
    }

    for (;;) {
        if (attempt >= 100) { if (!placed && b) b->vt_0x20(1); break; }  // 放弃，删对象

        int tries = 0;
        CellStruct at;
        for (;;) {
            int idx;
            do {
                // 取数点 0x59805E（PickRange @ 0x598030 内部，多阶段共用）
                idx = PickRange(0, side*side - 1);   // side = [0x89C2DC]
            } while (work[idx].xy == CellStruct(0,0));    // 0x50E470 判相等

            ++tries;
            at = work[idx].xy;                      // 工作记录 +0x00 = 该格 packed XY
            if (tries > 200) { at = CellStruct(0,0); break; }   // ⚠ 兜底：强行用格 (0,0)
            if (IsPlain(TryGetCellAt(at))) break;   // 0x486380
        }

        ...占地检查（与 0x595400 逐条相同）...
        if (ok) placed = b->vt_0xD8(Coord{ at.X*256+128, at.Y*256+128, 0 }, 0);
        ++attempt;
        if (placed) break;
    }
}
```

四条必须照抄的怪癖：

1. **`work[idx].xy == (0,0)` 的拒绝不计入 `tries`**，但**每次都消耗一个随机数**。
   这与阶段 3 里"格 (0,0) 无条件跳过"是同一件事的两种表现——
   工作记录里 `xy` 为 `(0,0)` 的项被当成"没填"
2. `tries` 上限 200 之后**不是放弃，而是强行用格 `(0,0)`**
   （`0x5A9791` 造一个 `CellStruct(0,0)`）。后面的占地检查基本会失败，
   于是白耗一次 `attempt`——但随机数照样花了
3. `tries` 每个 `attempt` 重置为 0（`0x5A970D` 的 `xor ebp,ebp`），
   而 `attempt` 上限 100 是整栋建筑的总预算
4. `side*side - 1` 每轮都重算（`0x5A971E` 的 `imul eax,eax`）——值不变，
   但对齐时别把它挪出循环，免得被"优化"改了求值时机（这里无副作用，可安全外提）

### 占地检查逐条

对占地表里每个 `(dx,dy)`（虚函数 `type->vt_0x90(1)` 返回，以 `(0x7FFF,0x7FFF)` 收尾）：

| 条件 | 地址 | 要求 |
|------|------|------|
| `cell->+0xE4 != 0` | `0x5A9819` | 无占用者 |
| `!IsPlain(cell)` | `0x5A9825` | 是空地（`0x486380`） |
| `cell->Level != baseLevel` | `0x5A982E` | `+0x11B` 与锚点同层 |
| `cell->+0xEC == 3` | `0x5A9839` | 不为 3 |
| `!IsWithinUsableArea(cell,1)` | `0x5A984A` | 在可用区内（`0x578540`） |
| `work[Y*side+X].+0x45 != 0` | `0x5A986F` | 工作记录该字节为 0 |

任何一条不满足即整栋失败。

## 四、`PickRange`（`0x598030`）的确切语义

阶段 2/4/7/10 共用的那个 `0x59805E` 就在这里：

```
00598039  sub  eax, ecx              ; hi - lo
0059803B  inc  eax                   ; +1
00598046  fild qword [esp+8]         ; span = (double)(hi-lo+1)，高 32 位为 0 → 无符号
00598056  fild qword [esp+8]         ; loD  = (double)lo，同样按无符号读
0059805E  call Randomizer::Random
00598070  fild qword [esp+8]         ; (double)(uint32)r
00598074  fmul qword [esp+0x10]      ; x span
00598078  fmul qword [0x7ED898]      ; x 1/(2^32-1)
0059807E  fadd qword [esp+0x18]      ; + loD      ← 加在截断之前
00598082  call 0x7C5F00              ; _ftol
00598087  cmp  eax, esi
00598089  ja   0059805E              ; 无符号比较，> hi 就重抽
```

两个易错点：

1. **`+ lo` 发生在 `_ftol` 之前**，不是 `lo + trunc(...)`。
   `lo` 是整数，数学上等价，但浮点表达式必须原样写
2. 拒绝用的是**无符号** `ja`，且 `lo` 也是按无符号 `fild qword` 读的
   ——负的 `lo` 会炸，原版从来没这么用

## 五、rules.ini 键名定下来了

`[0x8871E0]` = `RulesClass::Instance`，`+0xADC` 是一个 `DynamicVectorClass<BuildingTypeClass*>`
（数据 `+0xAE0`、个数 `+0xAEC`）。

在 `0x67391F` 找到它的读取点：

```
0067391F  lea  ebx, [esi + 0xadc]
00673926  push 0x83D36C              ; "NeutralTechBuildings"
```

同一段里相邻的两个向量是 `+0xAA4` = `BuildNavalYard`、`+0xAC0` = `BuildDummy`，
三者紧挨着，交叉印证了配对没错。

**所以阶段 6 的 `0x595400` 和阶段 7 抽的都是 `rules.ini [General] NeutralTechBuildings=` 这张表。**
这条也把 [stage-starting-points.md](stage-starting-points.md) §六 的第 6 条未确认清掉了。

## 六、本阶段随机数账

| 取数点 | 分支 | 语义 |
|--------|------|------|
| `0x5A95BF` | A（`MapType==2`） | `PickBelow(3)`：刷几轮 |
| `0x595461` | A（经 `0x595400`） | 挑建筑种类 |
| `0x5954DA` | A（经 `0x595400`） | 挑落点格（≤100 次） |
| `0x5A9645` | B | `PickBelow(5)`：撒几栋 |
| `0x5A96B8` | B | 挑建筑种类 |
| `0x59805E` | B（经 `0x598030`） | `PickRange(0, side²-1)` 挑格子 |

两条分支**互斥**，一次生成只会走一条。

## 七、未确认

1. `[house+0x34]` 是 `HouseTypeClass*` 还是别的（`+0xB8` 是它的数组下标）
2. `cell->+0xEC`、`cell->+0xE4` 的确切类型（后者显然是"占用者指针/计数"）
3. 工作记录 `+0x45` 的含义（阶段 6 同样要求它为 0）
4. `BuildingClass` 虚函数 `+0x20`（销毁）、`+0xD8`（放置）、
   `BuildingTypeClass` 虚函数 `+0x90`（占地表）的正式名字

## 八、复刻状态

已落到 `code/rewrite/rmg/tech_buildings.{h,cpp}`，两条分支各有一套黄金对拍：
基准 `memory/data/rmg/tech_buildings_golden.txt` 由
`code/randmap/dump_tech_buildings_golden.py` 用 Unicorn 直接跑 `0x5A95B0`
的原版机器码生成，测试 `code/rewrite/rmg/test/test_tech_buildings.cpp`（ctest `tech_buildings`）。

对拍时**留在原版一侧**（不打桩，跑真身机器码）的有：`PickBelow`/`PickBelowMul`/
`PickRange`(`0x598030`)、`CellStruct::operator==`(`0x50E470`)、`GetCellAt`(`0x5657A0`)、
`IsPlain`(`0x486380`)、`IsWithinUsableArea`(`0x578540`)。打桩的只有引擎对象那一侧：
`operator new`、`BuildingClass` 构造与三个虚函数、`FindIndexByName`/`FindHouseByType`、
以及分支 A 的 `0x595400`（它自己有独立的阶段 6 文档与对拍）。

### 分支 B 的 7 个用例

| 用例 | 布置 | 随机数 | 落地 | 销毁 |
|------|------|--------|------|------|
| `simple` | 1×1 占地，一次就成 | 4 | 1 | 0 |
| `bigfoot` | 2×2 占地，其中一格落在菱形外 | 3 | 1 | 0 |
| `blocked` | 已定砖 + 占用者交错 | 27 | 4 | 0 |
| `levels` | 层高 `(x+y)%3`，2×2 必跨层 | 780 | 0 | 3 |
| `veto` | `+0xEC == 3` 与工作记录 `+0x45` 两条否决 | 17 | 4 | 0 |
| `unplaceable` | `Unlimbo` 恒失败 | 1008 | 0 | 4 |
| `nowhere` | 全图已定砖 | 145230 | 0 | 3 |

`nowhere` 的 145230 个随机数是可以手算的：441 个工作槽里只有 184 个坐标非 `(0,0)`，
所以平均约 2.4 次抽取才凑够一次 `tries`，×200 次 `tries` ×100 次 `attempt` ×3 栋。
这条也就顺带证实了「拒绝 `(0,0)` 花随机数但不加 `tries`」确实是**热路径**而非边角。

### 分支 A 的 5 个用例

`ACASE` 行记的是每个区域的 `+0x20` 与原版实际调用 `0x595400` 时传进去的区域下标序列：

| 用例 | `+0x20` 表 | 随机数 | `0x595400` 调用序列 |
|------|-----------|--------|---------------------|
| `regA-1` | `1,0,2,0,3` | 1 | `0,2,4` |
| `regA-2` | `1,0,2,0,3` | 1 | `0,2,4,0,2,4`（抽到 2 轮） |
| `regA-0` | `1,2` | 1 | 空（抽到 0 轮） |
| `regA-none` | `0,0,0` | 1 | 空（没有区域有出生点） |
| `regA-empty` | 空表 | 1 | 空 |

`regA-0`/`regA-none`/`regA-empty` 三条都验证了「随机数照花、事情不做」。

### 三条新查实的行为

1. **`operator new(0x720)` 返回 0 是原版的真崩溃点。**
   `0x5A967D` 在拿到 `mem` 之前不检查，`0x5A9689` 直接 `mov ecx,[eax+0x520]`——
   `eax` 为 0 时就是空指针解引用。也就是说这条失败路径在原版里根本走不通，
   复刻侧的 `AllocBuildingMemory()` 返回 false 只是把「那次 `PickBelowMul` 一个
   随机数都不花」这一点保留下来，实际生成中不会触发。
2. **占地可以挂在菱形外。** 用例 `bigfoot` 里 2×2 的一角落到菱形之外，
   `GetCellAt` 退到共享哑元（全零），六条检查**全部通过**，建筑照样落地。
   所以复刻侧的哑元必须是「一份共享的、可被读到残留值的对象」，不能是
   「出界即否决」——那会少放建筑。
3. **`Unlimbo` 成功后只有菱形内的格子被标占用。** 原版桩与复刻侧都按这个来：
   哑元的 `+0xE4` 被写了也没人读得回去。

### 未测到的路径

- 分支 A 里 `0x595400` 内部的行为见 [stage-starting-points.md](stage-starting-points.md)，
  这里只对拍「调用序列」，不重复对拍它自己的两个随机数
- `operator new` 失败（见上，原版必崩）
