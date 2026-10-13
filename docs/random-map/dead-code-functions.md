# RMG 死代码函数取证（22 个零引用函数）

> 目标：判定 `rng-ledger.md` §二 那 22 个零引用函数"原本属于什么"。
> 前置条件已满足（104 活取数点全还原 + 端到端逐字节闭环）。
> 取证手段：`disasm.py`（capstone 直反汇编）+ callgraph `functions.tsv`/`direct_edges.tsv`
> + `xref_abs.py`/`xref_rel32.py` 双工具独立复核。**不依赖 Ghidra 的函数边界。**

## 结论摘要

1. **「死代码」判定成立**——至少对已复核的 `0x58BE20`：`xref_abs.py` 与 `xref_rel32.py`
   两个独立工具都报 0 处引用，与 `functions.tsv`/`direct_edges.tsv` 的零入边一致。
   即：函数体完整存在，但整个 4MB 镜像里没有任何静态引用。

2. **「疑似整套地形生成方案」这一假设不成立**——已取证的函数是**实体放置**
   （在随机合法格子上放建筑/单位），不是地形整形。依据见下。

3. **它们不是"TS 时代遗物"，而是 YR 自己的 MapGen 层代码**：用的是和活代码
   **完全同一套**全局与惯用法（`Randomizer @0xABE890`、work 数组 `@0xABED10`
   每格 0x50、边长 `@0x89C2DC`、同一套 `scaled_random` 拒绝采样）。
   形态更像**同一时期被放弃的另一套做法**，而非跨时代残留。⚠️ 此为推断，未实证。

## 数据质量问题：11 个函数被 Ghidra 误判为「1 字节函数」

`functions.tsv` 把下列 11 个记为 `body_bytes=1` 且出入边全 0：

`0x58BE20` `0x58C320` `0x58C4A0` `0x58C5A0` `0x59D480` `0x5A1190`
`0x5A2ED0` `0x5A4AE0` `0x5A4FA0` `0x5A5AE0` `0x5A80B0`

**这是错的。** 用「反汇编到第一个 `ret`」实测 11 个函数真实长度，**没有一个是
1 字节**：

| 地址 | 真实长度 | 内部调用特征 |
|---|---|---|
| `0x58BE20` | 126 B | `Random` + `_ftol` |
| `0x58C320` | 142 B | `Random` + `_ftol` |
| `0x58C4A0` | 231 B | + `GetCellAt(0x5657A0)` + `0x486380` |
| `0x58C5A0` | 259 B | + `GetCellAt` + `0x56D230` + `0x486380` |
| `0x59D480` | 137 B | `0x4CAC40` + `Random` |
| `0x5A1190` | 210 B | `0x4CADE0` + `Random` |
| `0x5A2ED0` | 117 B | `0x4CAC40` + `Random` |
| `0x5A4AE0` | 117 B | `0x4CAC40` + `Random`（与上**同构**） |
| `0x5A4FA0` | 117 B | `0x4CAC40` + `Random`（与上**同构**） |
| `0x5A5AE0` | 107 B | 只有 `Random` |
| `0x5A80B0` | 546 B | 12 个 call，第二个零引用根 |

`rng_ledger.txt` 记的取数点（`58BE48`/`58BECB` 等）落在实测函数体内，与反汇编
吻合——**是 functions.tsv 的边界识别错了，不是 ledger 记错**。

⚠️ **形态推测（未实证）**：`0x5A2ED0`/`0x5A4AE0`/`0x5A4FA0` 长度相同、调用特征
相同，疑为**同一辅助函数的三份复制**。短小且高度重复的函数形状，正是 Ghidra
边界识别器容易退化成 1 字节的情形——这与「11 个全被判 1 字节」的现象吻合。

⚠️ 影响面：这批 callgraph 数据后续被 frontier 流程当基准用。凡依赖
`functions.tsv` 的 `body_bytes`/`unique_callers` 做筛选的地方，对这 11 个地址
都可能得出错误结论。**引用计数本身仍可信**（xref 双工具独立复核过），只是
函数体大小不可信。

## 已确认的函数语义

### `0x58BE20` —— 随机取一个「已占用」的格子

```
eax = [0x89C2dc]（边长 side）；eax = side*side；esi = side*side - 1
scale = (double)(side*side)
loop:
  v = _ftol( (double)(uint32)Random() * scale * [0x7ed898] )   // = *total/4294967295.0
  if (v > side*side-1) goto loop;                               // 拒绝采样
  p = [0xabed10] + v*0x50                                       // lea ecx,[eax+eax*4]; shl ecx,4
  if (p->x == 0 && p->y == 0) goto loop;                        // 空格子 → 重抽
  *this = *p;  return;
```

- `side*0x50` 的索引方式与活代码（`0x578E60` 分配 side²×0x50）同一套。
- 返回值 = `work[v]` 首 dword，即 `x | (y<<16)`。
- 同地址 `0x58BEA0` 是同构的第二份（同样 prologue、同样惯用法）。

### `0x5A6510` —— 按派生区间取随机槽位，再除 2 传下去

```
n = arg->[0x10] / 10        // imul 0x66666667; sar edx,2   => >>34
m = arg->[0x10] / 5         // imul 0x66666667; sar edx,3   => >>35
hi = n - m + 1 ; lo = m
loop:  v = _ftol( (double)(uint32)Random() * hi * [0x7ed898] + lo )
       if (v > n) goto loop;
       FUN_005ad6f0( v >> 1, 0, 0, &out )
```

⚠️ `n`/`m` 的关系在正数下 `m > n`（`x/5 > x/10`），区间方向存疑；
需确认 `arg->[0x10]` 的取值域才能定语义。**暂标 partial**。

## 分类（按出边证据，⚠️ 为推断）

`direct_edges.tsv` 出边显示这批函数反复调用：

| 符号 | 出自 | 指向的子系统 |
|---|---|---|
| `BuildingClass::BuildingClass` | `0x5A6510` `0x5A82E0` | 放置建筑对象 |
| `MapClass::IsWithinUsableArea` | `0x5A6510` `0x5A6920` `0x5A82E0` `0x5A8720` | 合法性校验 |
| `0x5657A0`（GetCellAt） | 上述全部 | 坐标↔格子 |
| `UnitClass::AStarAttempt` | `0x5A6920` `0x5A8720` | ⚠️ 寻路 |
| `InfantryClass::GetFireError` | `0x5A6920` `0x5A8720` | ⚠️ 武器开火判定 |
| `operator_new` / `delete` | 多数 | 堆对象 |

**读法**：主线是「随机选合法格子 → 造建筑 → 校验可用区」，即**遭遇战实体布置**，
不是地形整形（后者应出现 tile/level/ramp/rim/trig 等地形写入）。

⚠️ **未解释的矛盾**：`0x5A6920`/`0x5A8720` 调用 `GetFireError`（开火判定）与
`AStarAttempt`（寻路）是**玩法函数**，地图生成不该碰。

**2026-10-13 修正**：查 `functions.tsv` 的 `symbol_source` 字段，上述
`GetFireError` / `AStarAttempt` / `BuildingClass::BuildingClass` /
`MapClass::IsWithinUsableArea` 全部标记为 **`USER_DEFINED`**——即符号名来自
**YRpp 开源符号覆盖**，而非从本 exe 实锤。所以"出现玩法函数"这一矛盾
**可能只是命名出处问题**，不能当作 TU 归属的铁证。本文档中所有带类名的调用
一律视为「推断名」，实锤的只是**地址**（地址来自 `direct_edges.tsv`，
是本 exe 内的真实 call 边）。**待查**：用汇编实锤被调函数到底是不是寻路/开火。

## TU 归属：强证据指向 MapGen.cpp，但非铁证

`scan_source_macros.py` 实测 36 个 TU，`MapGen.cpp` 的断言宏只覆盖
**`0x5956E7-0x59A63A`**（21 处，行 3934-6746）。而这 22 个函数散布在
**`0x58BE20-0x5A8F00`**，明显溢出该区间。断言宏只出现在有 `assert` 的代码处，
所以「落在宏区间外」**不能**推出「不是 MapGen.cpp」。

**2026-10-13 取证**——子树根 `0x5A5020` 的反汇编调用序列里有：

```
005a50c0  call 0x598030     ← 活函数（10 个外部调用者）
005a52d4  call 0x5980c0     ← 活函数（11 个外部调用者）
```

两个都在 MapGen.cpp 断言区间 `0x5956E7-0x59A63A` 内。加上死代码与活代码共用
同一套全局（`Randomizer @0xABE890`、work 数组 `@0xABED10`、边长 `@0x89C2DC`）
和同一套 `scaled_random` 惯用法，且物理相邻——**强证据指向同属 MapGen.cpp
（YR 自己的代码层），而非 TS 时代跨时代残留**。

⚠️ **仍是推断而非铁证**：调用关系 + 全局共用 + 物理相邻都不能单独证明 TU 归属。
要钉死需：① 对 `0x598030`/`0x5980C0` 实锤其确在 MapGen.cpp（用断言行号回推）；
② 或找该 exe 的 `.obj`/COMDAT 边界证据。**未做**。

## 孪生签名分析：排除「活函数的未引用复制品」

按「出边目标集合」作签名，在全表 8655 个函数里检索同签名者：

| 结果 | 数量 |
|---|---|
| 有出边且签名全表唯一 | 11 |
| 同签名但成对（`0x5A6920` ↔ `0x5A8720`） | 2 |
| 无出边（Ghidra 边界误判，见上） | 11 |

**没有任何一个死代码函数在活代码里找到同签名孪生**。这排除了「死代码是某个
活函数的未引用复制品」这一假设——它们是**独一份、从没被接线**的算法。

## 未确认项

1. `0x5A6920`/`0x5A8720` 为何出现玩法函数（寻路/开火）——TU 归属的判据。
2. `0x5A6510` 的 `arg->[0x10]` 取值域（决定 `n`/`m` 区间是否合法）。
3. 余下 10 个函数的逐个语义（当前只逐字节反汇编了 `0x58BE20` 与 `0x5A6510`）。
4. functions.tsv 那 11 条 `body_bytes=1` 是否要回修 callgraph 数据。
