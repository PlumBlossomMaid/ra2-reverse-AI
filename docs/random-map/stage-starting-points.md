# 阶段 6：Creating starting points——挑出各家的出生点

> 状态：**外层重试结构、区域筛选与配额分配、三个取数点的惯用法均已实锤**；
> 若干全局与辅助谓词未确认（见 §六）。
> 取证：`memory/data/decomp/rmg_starting_points.txt`。
>
> 这一阶段最要命的不是算法，是**它会整段重来**——失败的那一轮已经消耗掉的随机数
> 不会退回去。复刻时必须把重试循环原样搬过来，否则种子从这里开始就分岔。

## 一、主流程里的重试循环

```c
do {
    do { ok = PlaceStartPoints(); } while (!ok);   // 0x594B50
    ok = ValidateStartPoints();                     // 0x5A1FB0
} while (!ok);
```

- `0x594B50` 的所有出口都 `return 1`（`CONCAT31(uVar10,1)`），**内层循环实际只跑一遍**
- `0x5A1FB0` 会对每个出生点做一次最小堆扩散、量一量可达面积，不满意就返回 0
  → **整个 `0x594B50` 重跑，随机数继续往下消耗**
- `0x5A1FB0` 自己**不取随机数**（它不在取数点清单里）

## 二、`PlaceStartPoints`（`0x594B50`）

### 第 1 步：算「区域太小就不要」的阈值

```
00594B53  fild  dword ptr [0xABE15C]
00594B64  fimul dword ptr [0xABE158]
00594B7E  fmul  qword ptr [0x7ED8D0]     ; x 0.03
00594B84  fld   qword ptr [0x7ED8C8]     ; 400.0
00594B8A  fcomp st(1)                    ; 取两者较大者
00594B9B  call  0x7C5F00                 ; _ftol（截断）
```

```c
int minCells = (int)max((double)([0xABE15C] * [0xABE158]) * 0.03, 400.0);
```

即「面积的 3%，但至少 400 格」。`[0xABE158]` / `[0xABE15C]` 疑似地图宽高，**未确认**。

### 第 2 步：按移动区重新划区域

```c
static const int zoneSel[4] = { 5, 5, 5, 0 };
int zone = zoneSel[[0xABE014]];
uint want = ...;                            // 由寻路层的返回值查表得到

for (Cell* c : MapCells) {
    if (!Pred_0x5AC230(c))                        continue;
    if (work[c].regionId != -1)                   continue;   // +0x38
    if (c->+0x4C != 0)                            continue;
    if (MapClass::GetMovementZoneType(c, zone, 0) != want) continue;
    GrowRegion(want);                             // 0x594420
}
```

然后把 `cellCount`（`+0x0C`）小于 `minCells` 的区域全部销毁。

### 第 3 步：排序

给每个存活区域算一个排序键，再 `qsort`（`0x7C8B48`，比较函数 `0x5AD9C0`）：

```c
key = (500000 - region->cellCount) * regionCount + region->regionId;
```

`500000 - cellCount` 让**大区域排在前面**；乘 `regionCount` 再加 `regionId`
是把「编号」当低位，保证同样大小的区域按编号稳定排序。

### 第 4 步：把出生点名额按面积比例分给各区域

```c
int total = [0xABE028];        // 出生点总数
int acc = 0, assigned = 0;
for (int i = 0; i < n; ++i) {
    acc += list[i]->cellCount;
    int give;
    if (i == n - 1) give = total - assigned;                  // 最后一个吃余数
    else            give = (int)((double)acc / allCells * total + 0.5) - assigned;
    list[i]->startCount = give;                               // +0x20
    assigned += give;
}
```

对应汇编（`0x594E25`–`0x594E40`）：

```
00594E25  fild  dword ptr [esp+0x1C]      ; acc（累计格子数）
00594E29  fidiv dword ptr [esp+0x10]      ; / 全部区域格子数
00594E2D  fimul dword ptr [esp+0x14]      ; * total
00594E31  fadd  qword ptr [0x7E1738]      ; + 0.5
00594E37  fisub dword ptr [esp+0x18]      ; - 已分配
00594E3B  call  0x7C5F00                  ; _ftol
00594E40  mov   [edi+0x20], eax
```

`fadd 0.5` 之后走 `_ftol` 截断，等价于对**累计比例**做四舍五入再作差——
是标准的「累计取整」分配法，不会因为逐个四舍五入而丢名额。

⚠ 注意 `fadd 0.5` 在 `fisub` **之前**：先给累计值加 0.5 再减已分配数，
和「(比例 - 已分配) + 0.5」在浮点上不是同一个表达式，照抄顺序。

### 第 5 步：在每个区域里放点

```c
int base = 0;
for (auto* r : list) { PickPointsInRegion(r, base); base += r->startCount; }   // 0x594870
```

### 第 6 步：给空手的区域补个建筑（**取数点 `0x594EA1`**）

```c
if ([0xABE014] == 0)
    for (auto* r : list)
        if (r->startCount == 0 && UniformUnit() < 0.5)
            PlaceRandomBuilding(r);        // 0x595400
```

汇编实锤是严格小于：

```
00594EA1  mov   ecx, 0xABE890
00594EA6  call  Randomizer::Random
00594EB7  fild  qword ptr [esp+0x1C]       ; (double)(uint32)r
00594EBB  fmul  qword ptr [0x7ED898]       ; x 1/(2^32-1)
00594EC1  fcomp qword ptr [0x7E1738]       ; 与 0.5 比
00594EC9  test  ah, 1                      ; C0：ST(0) < 0.5
00594ECC  je    跳过
```

## 三、`PickPointsInRegion`（`0x594870`）——两个取数点

```c
// 取数点 1：0x5948CD —— PickBelow(3)，比例常数 0x7ED8C0 = 3/(2^32-1)
int extra;
do { extra = (int)((double)(uint32)Random() * (3.0/4294967295.0)); } while (extra > 2);

int budget = extra + region->startCount * 15;
Vector<CellStruct> cand;
for (int tries = 0; cand.size() < budget && tries < 300; ++tries) {
    // 取数点 2：0x594935 —— PickBelow(region->cellCount)，标准 span x (1/(2^32-1))
    int n = region->cells.Count;                       // +0x38
    int k; do { k = (int)((double)(uint32)Random() * (double)n * (1.0/4294967295.0)); }
           while (k > n - 1);
    CellStruct xy = region->cells[k];                  // +0x2C

    Rect box = { xy.X - 3, xy.Y - 3, 6, 6 };
    if (Pred_0x5A7250(...) && Rect_0x578640(&usableArea, &xy, 1))
        cand.push_back(xy);
}
ChooseWaypoints(cand);                                 // 0x594F40，不取随机数
```

三个要点：

1. **`extra ∈ {0,1,2}` 是一次独立抽取**，而且**无论 `startCount` 是不是 0 都要抽**
   ——`startCount == 0` 的区域预算是 `extra + 0`，循环可能一次都不进，
   但那一个随机数已经花掉了
2. 候选采样是**有放回**的，`tries` 上限 300 与预算上限同时生效
3. `usableArea` 是地图可用矩形往里缩：`(x+4, y+4, w-8, h-8)`，
   四个分量取自 `[0x87F8E4]`/`[0x87F8E8]`/`[0x87F8EC]`/`[0x87F8F0]`

选出的坐标写进 waypoint 表，并给格子打上 `+0x140 |= 4`。

## 四、`PlaceRandomBuilding`（`0x595400`）——两个取数点，阶段 6 / 7 共用

```c
// 取数点 1：0x595461 —— 从规则表里随机挑一种建筑
int n = [[0x8871E0] + 0xAEC];                      // 列表长度
int k; do { k = PickBelow_inline(n); } while (k > n - 1);
Building* b = new BuildingClass([[0x8871E0] + 0xAE0][k], owner);

for (int attempt = 0; attempt < 100; ++attempt) {
    // 取数点 2：0x5954DA —— 从区域格子里随机挑一格
    int m = region->cells.Count;                   // +0x38
    int j; do { j = PickBelow_inline(m); } while (j > m - 1);
    CellStruct at = region->cells[j];              // +0x2C

    // 逐格检查建筑占地（占地表由建筑类型的虚函数 +0x90 给出，以 (0x7FFF,0x7FFF) 收尾）
    bool ok = true;
    for (each (dx,dy) in footprint) {
        Cell* c = TryGetCellAt(at + (dx,dy));
        if (c->+0xE4 != 0)                    { ok = false; break; }  // 已有占用者
        if (!IsPlain(c))                      { ok = false; break; }  // 0x486380
        if (c->Level != at.Level)             { ok = false; break; }  // +0x11B 必须同层
        if (c->+0xEC == 3)                    { ok = false; break; }
        if (!IsWithinUsableArea(c, 1))        { ok = false; break; }
        if (work[c].+0x45 != 0)               { ok = false; break; }
    }
    if (ok) { 放下建筑（虚函数 +0xD8，坐标 = (X*256+128, Y*256+128)）; break; }
}
```

⚠️ **建筑种类只抽一次，位置最多抽 100 次**——抽不到位置就把建筑对象丢掉，
但那 100 个随机数已经消耗了。

`0x8871E0` 疑似 `RulesClass::Instance`，`+0xAE0`/`+0xAEC` 是某个
`DynamicVectorClass<BuildingTypeClass*>` 的数据指针与个数，**具体是哪张表未确认**。

## 五、顺带补上的 RegionClass 字段

阶段 3 那份表里标「未确认」的两项，在这里定了：

| 偏移 | 语义 | 证据 |
|------|------|------|
| `+0x20` | 分到的出生点个数 | `0x594E40` 写、`0x5948F3` / `0x594E9A` 读 |
| `+0x28` | `DynamicVectorClass<CellStruct>`：本区域的格子表 | 数据 `+0x2C`、个数 `+0x38`，与 `0x5A0700` 里那个 0x18 字节向量布局一致 |

顺带把 `DynamicVectorClass` 的布局也钉死（0x18 字节）：

| 偏移 | 内容 |
|------|------|
| `+0x00` | vtable |
| `+0x04` | 数据指针 |
| `+0x08` | 容量 |
| `+0x0C` | （未确认） |
| `+0x10` | 元素个数 |
| `+0x14` | 扩容步长 |

## 六、未确认

1. `[0xABE014]`——`zoneSel[]` 的下标，同时也是「要不要补建筑」的开关；
   取值范围与来源未确认（`zoneSel = {5,5,5,0}`，只有下标 3 给出 0）
2. `[0xABE028]`——出生点总数，疑似由玩家数推出，未确认
3. `[0xABE158]` / `[0xABE15C]`——阈值公式里的两个乘数，疑似地图宽高，未确认
4. `0x5AC230` / `0x594420` / `0x5A7250` / `0x578640` / `0x594F40` 的语义
5. `0x5A1FB0` 判定「这组出生点合格」的具体标准——它做的是最小堆扩散，
   但接受/拒绝的门槛还没读
6. `[0x8871E0] + 0xAE0/0xAEC` 是 rules.ini 的哪张表
7. 工作记录 `+0x45`（建筑放置时要求为 0）的含义
