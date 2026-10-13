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

**这是错的。** `disasm.py 0x58BE20` 实测：函数体从 `0x58BE20` 连续到
`0x58BE9D` 的 `ret`，中间 40+ 字节是完整指令流。`rng_ledger.txt` 记的取数点
`58BE48`/`58BECB` 落在体内，与反汇编吻合——**是 functions.tsv 的边界识别错了，
不是 ledger 记错**。

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
`AStarAttempt`（寻路）是**玩法函数**，地图生成不该碰。这两种可能：
(a) 这些函数不属于 MapGen 而是被链接器聚到同一 `.obj` 邻块的其他 TU；
(b) 符号名来自 YRpp 推断而非本 exe 实锤。
**待查**：符号来源 + TU 归属。

## TU 归属：未确认

`scan_source_macros.py` 实测 36 个 TU，`MapGen.cpp` 的断言宏只覆盖
**`0x5956E7-0x59A63A`**（21 处，行 3934-6746）。而这 22 个函数散布在
**`0x58BE20-0x5A8F00`**，明显溢出该区间。

断言宏只出现在有 `assert` 的代码处，所以「落在宏区间外」**不能**推出「不是
MapGen.cpp」——它可能只是该 TU 里没有断言的代码。同样也不能排除邻接 TU。

判定方法：按地址就近比对同 TU 内**活函数**的调用特征（活 RMG 阶段函数调什么、
用什么全局），再配合符号来源交叉验证。**未完成。**

## 未确认项

1. `0x5A6920`/`0x5A8720` 为何出现玩法函数（寻路/开火）——TU 归属的判据。
2. `0x5A6510` 的 `arg->[0x10]` 取值域（决定 `n`/`m` 区间是否合法）。
3. 余下 10 个函数的逐个语义（当前只逐字节反汇编了 `0x58BE20` 与 `0x5A6510`）。
4. functions.tsv 那 11 条 `body_bytes=1` 是否要回修 callgraph 数据。
