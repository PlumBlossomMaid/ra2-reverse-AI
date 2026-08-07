# 阶段 11 Cleanup / 12 Compute Radar Image + 阶段之间的隐藏动作

这份文档收两件事：

1. **主线最后两个阶段**——实测**纯拆解**，不碰地形，对 `.map` 字节没有影响；
2. **调度器 `0x598960` 里夹在阶段之间、没有日志字符串的housekeeping**——
   其中有一段**会把区域信息整个抹掉**，复刻时不能漏。

## 一、13 条日志的精确落点

`RMG:` 系列字符串的引用点（`xref_abs.py` 逐串扫出，按地址排序）：

| 打印点 | 日志 |
|---|---|
| `0x598A5E` | `RMG: Init random map` |
| `0x598AE4` | `RMG: Seeding water` |
| `0x598C25` | `RMG: Init regions` |
| `0x598D45` | `RMG: Making regions` |
| `0x598E20` | `RMG: Recalculating cell attributes` **#1** |
| `0x598E9F` | `RMG: Creating starting points` |
| `0x598EC0` | `RMG: Adding tech buildings` |
| `0x598EE5` | `RMG: Creating tiberium` |
| `0x598FB9` | `RMG: Recalculating cell attributes` **#2** |
| `0x59912B` | `RMG: Recalculating cell attributes` **#3** |
| `0x599172` | `RMG: Creating hills` |
| `0x599216` | `RMG: Creating LATs, rocks etc` |
| `0x599355` | `RMG: Recalculating cell attributes` **#4** |
| `0x5993A6` | `RMG: Cleanup` |
| `0x599452` | `RMG: Compute Radar Image` |
| `0x5994B1` | `RMG: Done` |

四次 Recalc 的函数体一模一样——重置迭代器，对每格调
`CellClass::RecalcAttributes(-1)`（`0x47D2B0`）。都不取随机数。

### ⚠️ 原版怪癖：#2 与 #3 之间什么都没干

`0x598FB9`（#2）与 `0x59912B`（#3）之间只有**进度条推进**和
`PreviewClass::GeneratePreviewImage()` + `SendMessage(WM_PAINT)`——
没有任何一处改地形。也就是说**原版连着跑了两遍完全相同的全图 `RecalcAttributes`**。

不影响随机数序列，但复刻时若打算"优化掉一遍"，得先证明
`RecalcAttributes` 是幂等的；在没证明之前，**照抄两遍**。

## 二、隐藏动作：布矿之后的"区域清算"

`0x598F04-0x598FB8`，夹在 `0x5A23A0`（布矿）与 Recalc #2 之间，**没有日志**：

```
00598f04  cmp  dword ptr [0xabed10], ebx      ; 工作数组存在？
00598f0c  mov  ecx, dword ptr [0x89c2dc]
00598f12  imul ecx, ecx                       ; side * side
00598f21  or   edx, 0xffffffff
00598f24  mov  dword ptr [eax + esi + 0x38], edx   ; work[i].+0x38 = -1
00598f2e  mov  dword ptr [eax + esi + 0x3c], edx   ; work[i].+0x3c = -1
00598f32  add  eax, 0x50
00598f36  jne  0x598f1b
00598f38  mov  edx, dword ptr [0xabdfa0]      ; 区域个数
...                                           ; 逐个析构 RegionClass 并 free
```

做两件事：

1. **全图工作记录的 `+0x38` / `+0x3C` 写回 `-1`**——即"区域 id / 田块 id"作废。
2. **销毁 `[0xABDF94]` 里的全部 `RegionClass`**（虚析构 → 从 `[0xABDF90]` 的容器里摘掉
   → 还原 vtable `0x7E38D0` → `free`），最后 `[0xABED14] = 0`。

这解释了阶段 10 §2.1 为什么还要再清一次 `+0x3C`：**阶段 9 造山不碰它，但这里已经是 -1**，
而 LAT 写入器 `0x5A4B60` 拿 `+0x3C` 当"本田块已收"标记，`fieldId` 从 0 起算，
所以必须先归零、不能留着 `-1`。两处清理**不是重复**，是不同语义。

> 复刻要点：**区域信息在布矿之后就没了**。阶段 9/10 里任何"按区域"的推理都是错的——
> 那时候 `RegionClass` 数组已经是空的。

## 三、阶段 11 Cleanup（0 取数，不碰地形）

`0x5993A6` 之后：

```c
seed->+0x304 = 0;
seed->+0x308 = 0;
if (Work != NULL) { free(Work); Work = NULL; }        // [0xABED10]
for (i = RegionCount-1; i >= 0; i--)                   // 与 §二 同一段代码
    destroy(Regions[i]);
Regions.Count = 0;                                     // [0xABED14] = 0
FUN_00568BB0(1);
```

紧邻其前的 `FUN_00722D00()` / `FUN_00722240()`（`0x599395` 之前）遍历
`[0xB0F4EC]`（count `[0xB0F4F8]`）的对象数组，把每个对象的 `+0x110` / `+0xF4`
指向的动态数组清空并释放——**是所属方（House）的缓存，不是地形**。

结论：**Cleanup 只释放生成期的临时结构**。
`rng-ledger.md` 早先写的"Cleanup 会改地形（重算格子属性）"是**误记**——
改地形的是它前面那次 Recalc #4（`0x599355`），已在 §一 单列。

## 四、阶段 12 Compute Radar Image（0 取数，不碰地形）

```c
RadarClass::SetVisibleRect(&MapRect);   // 0x654490，参数 [0x87F8E4]
FUN_00654650();                          // 重建雷达小地图位图
```

只影响运行期显示，与 `.map` 输出无关。

## 五、对复刻程序的意义

| 阶段 | 是否影响 `.map` 字节 | 复刻中要不要实现 |
|---|---|---|
| Recalc #1..#4 | ✅ 是（`RecalcAttributes` 会改格子属性） | 要 |
| 布矿后的区域清算（§二） | ⚠️ 间接影响（决定 `+0x3C` 初值） | 要 |
| Cleanup | ❌ 否 | 不要 |
| Compute Radar Image | ❌ 否 | 不要 |
| Done | ❌ 否 | 不要 |

## 六、未确认清单

1. `CellClass::RecalcAttributes(-1)`（`0x47D2B0`）到底改了哪些字段——**未确认**，
   这是"Recalc 是否幂等"的前置问题。
2. `MapSeedClass +0x304` 的字段名——**未确认**（Cleanup 里被清零，全流水线没找到读点）。
   同处清零的 `+0x308` 已定名：**块编号（生成戳）计数器**，
   见 [stage-seeding-water.md](stage-seeding-water.md) §四「收尾与整块回滚」。
3. `FUN_00568BB0`（`ecx` 未知，参数 1）的语义——**未确认**。
   它遍历 `[0xA8E9AC]`/`[0xA8E9B8]` 的对象数组，按 `+0x197` 标志调 vtable `+0x20`。
4. `FUN_00722D00` / `FUN_00722240` 里 `+0x110` / `+0xF4` 的字段名——**未确认**，
   只实锤了"是 `DynamicVectorClass` 形态，被清空并释放"。
