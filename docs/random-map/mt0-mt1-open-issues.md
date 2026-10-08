# mt0 / mt1 剩余发散——取证固化与已排除清单

> 来源：原 AGENTS.md（kimi-k3，2026-10-07）。8960 案穷举中，除 mt0/th0/sz0 与
> mt1 的 20 案外全部通过。本文固化两个主战场的现状，**接手者先读本文，勿重新推导**。

## 背景

对拍基线（`_sweep_cmp.py 12345`，修复前 CLI 批量）：`pass=1100 diff=1299 missing=2081`。
diff 几乎全是 mt0/th0/sz0；**雪地 th1 全过**；时间参数 t 对 cells 无影响。

## mt0（主战场，~77% 的 mt0 案发散）

### 已修：PlaceRandomBuilding（0x595400）全面重写（commit 77c7038）

三重根因：① 占地恒 1×1（kUnit 回退，`rules.foundations` 从未填充）；
② 层高检查错用 r->level（区域 level 恒 0）导致陆地格 100/100 全失败；
③ 取数用了折叠 PickBelow，原版两个取数点（0x595461/0x5954DA）都是**两次乘法**
PickBelowMul。重写含真实占地偏移表（art(md).ini `Foundation=` → 0x89C900 静态表）、
锚点层高、Unlimbo/Destroy 句柄语义。单案 845→**772** 格差异。
验证：62298 repro sed vs live rmg_done 仍 0 差异；p4/p6/sz1/sz2 仍 0 差异。

### ✅ mt0 布矿发散根因已实锤并修复（2026-10-08，TIBTRE 清矿）

**根因**：`TerrainClass::TerrainClass`（0x71BB90）内部调放置函数 0x71D000，
后者在目标格有 overlay 且其 `OverlayTypeClass+0x2A9`（INI `Tiberium=yes`）为真时
**清掉该格 overlay（ov=-1、od=0）**（0x71D123 `mov [eax+0x44],esi` /
0x71D12B `mov byte [eax+0x11E],0`）。矿田锚定 TIBTRE 落在已铺矿格上时，
原版清矿重铺；CLI 的 `CreateTerrain` 不清 → 扩散路径全错位。

**取证链**（全部活体实证，案 = s12345_mt0_t0_th0_sz0_r0_p2）：
1. `rmgctl tibdump on`（DLL 自带 0x598EF4/0x598EF9 取证钩）→ 布矿入口态
   与 CLI `--dump-arl-state` **逐维全同**（RNG 250 词、ND、cells、work、
   区域、候选点、waypoints 全部一致）——输入无差异。
2. 同入口态灌 emu 跑原版 0x5A23A0 → emu 输出 == CLI（总取数 9944 精确一致），
   但 ≠ 活体（多 44 次取数）→ CLI 实现与原版汇编等价，差异在 emu/CLI 环境缺东西。
3. 逐田 RNG 指纹（bp 0x5A28C0）：田 0-3 一致，田 4（出生点田 count=500）分岔。
4. field#4 取数调用点序列（bp 0x65C780 ×1851 次活体）：第 841 次分岔——
   emu 走入队、活体走放置。
5. key 值逐位对比（bp 0x5A2DB9）：**前 835 个 key 位级一致** → x87/常量无罪。
6. pop 序列对比（bp 0x5A2C12）：pop 0-1475 全同，pop 1476 同格 (37,85)
   但 emu ov=107 od=9 vs 活体 ov=-1 od=0 → 活体的矿被清了。
7. 汇编实锤：0x71D000 的清除段 + 活体验证 TIB01/GEM01 +0x2A9=1、
   SROCK01 +0x2A9=0（矿可清、石头不可清）。

**修复**：`cliworld.cpp` `CliWorld::CreateTerrain` 增加 overlay 清除
（`OverlayLandOf(ov).tiberium` 已随 mt1 修复装载）。修复后 mt0 采样
**pass=36/40**（此前 pass=0/30）。

**残差（mt0）**：4 案 `th1_sz0_r3_p2`（雪地+r3+p2）仍 60 格差异
（zone:60,obj:57）——比修复前 772-876 格小一个数量级，是独立的下一层问题
（疑与 LATs 树/占位链有关，待定位）。

**注意**：headless 断点复用游戏进程会撞 ScyllaHide "already hooked" 弹窗
（钩子在 detach 后残留在目标进程）——**每次 headless 采集必须全新游戏进程**；
attach 后先 `bc` 清 x64dbg 断点库残留再下断点。读线程上下文要
`Wow64GetThreadContext` + `CONTEXT_FULL`（CONTROL 段不含 Edi）。

### 旧档：修复前的排除清单与采样复验（历史记录）

<details>
<summary>已排除项（全部实证，别再查）与 2026-10-07 采样复验</summary>

- 布矿前全部阶段与布矿本身：链式 emu（水→细化→InitRegions→SpreadGreen，每点
  cells 0 差异+RNG 账本吻合）、Recalc#1 emu、出生点 emu、0x5A1FB0 洪泛 emu、
  布矿 emu（p2/p3/p4/sz1 全 0 差异，**p4/sz1 与真值 ore_diff=0 完美复现**）。
- RNG 流平移 Δ∈[-250,250]（p2/p3 各 501 点）：**Δ=0 唯一最优**，无移位解。
- ND flag=1 变体（k∈[-50,50] Marsaglia 缓存）：无命中。
- FPCW 变体（chop/near/up/down × PC53/64）：unicorn 忽略 FPCW，无差异。
- work queued/flag45 初值变体：queued 无影响；flag45 是关键约束（f0 → 567），
  但 0x5A1FB0 emu 已证 CLI 的洪泛正确。
- 区域格子顺序（DFS）：emu 原版 == CLI member 行逐格一致。
- min/max 矿量（900/1050）：0x5981F0 从 RMGMD.INI 读，双端一致。
- MapType 分支：mt0 不走 0x59C580（河湖）/making_regions，无遗漏调用。
- 2026-10-07 采样复验（PlaceRandomBuilding 修复后）：pass=0/30，
  发散指纹不变 → 静态路径穷尽，后由活体取证破案（见上）。

</details>

### 62298 正确回归基准

= `build_ninja62298/repro_62298.sed`（**别用** `E:\YRLauncher\RandMap.Sed`，
它 10-06 被改成 Seed=1/Water=99，与 live 基准不匹配；旧 `_od_smooth_verify.py` 因此失效）。

## mt1（20 案单格）

完整取证在 `.qwen/tmp/_mt1_analysis.md`（**先读它，勿重新推导**）：

- **症状A（r3_p5 ×4，t0-t3）**：差异格 (91,44) —— CLI 多栽一棵树（obj=1/zone=5），
  其余 211 棵逐格一致（随机流未分叉，是单次决策差异：该格 6 条过滤终态全满足，
  差异在决策时刻的取数判定或堆 pop 序）。差异点 `lat_spread.cpp:263` GrowTreePatch。
- **症状B（r0-r3_p6 ×16）**：差异格 (93,121) —— 有矿 overlay(ov=168) 的格，原版
  ground=3(Rock)/zone=6，CLI ground=5(Tiberium)/zone=0；iso=421/ov=168 两侧一致，
  CliffBack 不成立（SE 邻居 lv=5 < cell+4=8）。候选根因两条：
  ①放矿后原版**不再**重算 ground/zone（ground 保留放矿前 tile 查表的 Rock=3）；
  ②`OverlayLandOf()`（cliworld.cpp:978）对 27-38/102-113 硬编码 owns_land，
  但 ov=168 不在该区间，需核对 rulesmd.ini `[OverlayTypes]` 第 168 小节的实际键。

**症状B 根因已实锤（2026-10-07 qwen-code 独立静态复核 kimi 的修复）**：
原版 OverlayTypeClass 表的下标 = `[OverlayTypes]` **条目出现次序**（跳过
none/<none> 与重名），与 INI 数字键无关。runtime id 168 = **SROCK01**
（`Land=Rock` + `NoUseTileLandType=true` → ground=3/zone=6，与活体吻合），
不是按键映射的 TIB3_19。脚本 `.qwen/tmp/_mt1_overlay_order_check.py` 验证：
27-38=GEM01-12、102-113=TIB01-12、168-177=SROCK01-05/TROCK01-05 全部吻合。
修复已在 `rules_loader.cpp:171-208`（第二趟按出现次序分配 id）。

### mt1 闭合验证结果（2026-10-07，build_verify exe 重跑全部 128 个发散案）

报告里 mt1 发散案实际共 **128 案**（非"20 案"），重跑后 **pass=88 diff=40**：

- **症状B（sz0_r*_p6 ×16）：全部 PASS** ✅——overlay 出现次序修复生效。
- **sz1_th0_r*_p8 ×12（原 2 格差异）：全部 PASS** ✅。
- **sz2 各种 ×44 + sz3 各种 ×24：全部 PASS** ✅（kimi 的系列修复连带闭合）。
- **症状A（sz0_r3_p5 ×4）：仍 1 格（zone:1,obj:1）**——GrowTreePatch 单次决策差异未解。
- **sz1_th1_r*_p7 ×12：仍 1 格（zone:1,obj:1）**——与症状A 同类（单棵树 zone/obj）。
- ~~sz0_r*_p8 ×24（大发散组）~~——**已闭合**：2026-10-08 TIBTRE 清矿修复
  （CreateTerrain overlay 清除，见 mt0 节）重跑后全部 PASS，th0/th1 均无差异。
  该组与 mt0 布矿发散同根因。

### mt1 终态（2026-10-08，TIBTRE 清矿修复后重跑 128 案）

**pass=112 diff=16**，剩余 16 案全部是单格树（zone:1,obj:1）：

- 症状A sz0_r3_p5 ×4 + sz1_th1_r*_p7 ×12 —— GrowTreePatch 单次决策差异，
  mt1 唯一剩余开放问题。

### ✅ mt1 单格树 + mt0 残差根因已实锤并修复（2026-10-09，X87SpreadKey fldcw 笔误）

**两个残差的共同根因，是一个字节的笔误。**（下面 2026-10-08 的「ctor 前拒绝」
机制推论是**错的**——真正根因是 SpreadKey 的浮点控制字。）

**根因**：`fast_math.cpp` 的 `X87SpreadKey`（x64 x87 机器码 kCode2）里，
加载 FPU 控制字的指令写成了 `DB 6C 24 08`（= `fld xword [rsp+8]`，80 位装载），
而正确的是 `D9 6C 24 08`（= `fldcw [rsp+8]`，加载控制字 0x0E7F）。
fldcw 从未生效 → 全程跑 fninit 默认值 **0x037F（PC=64 / 最近舍入）**，
而游戏是 **0x0E7F（PC=53 / chop 向零截断）**。fstp f32 用最近舍入而非 chop
→ CLI 的树/矿 key 系统性比原版**高 1 ULP** → 平局/近平局的堆 pop 序翻转。

**取证链**（全部活体实证，案 = `s12345_mt1_t0_th0_sz0_r3_p5`）：
1. 给 rmg_hook.dll 加进程内树取数日志（hook 0x5A47FC/0x5A4826/0x5A49C2/0x5A49EE，
   记 draw 与算完的 key，rmg_done 落盘 `.treedraw`），绕开调试器
   （调试器挂 INT3 会让生成卡死）。
2. 活体 vs CLI 对拍：u-draw 序列 0-122 逐格一致，pop (107,42) 与 (91,39) 的
   **堆 pop 序在 key 0x412DBDBD 处翻转**——CLI 把它们当平局，原版不是。
3. 活体记 key + FPU 控制字：CLI 的 key 比活体**高 1 ULP ×3633 格**（另 3552 格一致），
   活体控制字实测 **0x0E7F**（PC=53/chop），证明 CLI 的 fldcw 没生效。
4. 精确复算（Fraction 无限精度 + PC=53 chop 模拟）：正确 key = 0x408E73C8 =
   **活体值**；CLI 的 X87SpreadKey 给 0x408E73C9（+1 ULP）。
5. 反汇编 kCode2 实锤 `DB 6C 24 08`（fld xword）应为 `D9 6C 24 08`（fldcw）。

**修复**：kCode2 的 `0xDB,0x6C,0x24,0x08` → `0xD9,0x6C,0x24,0x08`（一个字节）。

**验证**（全部用修复后 build_verify）：
- mt1 单格树 ×16：**全部 PASS** ✅
- mt0 残差 th1_sz0_r3_p2 ×4（原 60 格 zone/obj）：**全部 PASS** ✅（同一根因）
- mt0 采样 30 案：**30/30 PASS** ✅（矿段无回归）
- 62298 回归：11 格不变（那是进对局后运行时矿生长，不可复现，非本问题）
- ctest build_x86：42/43（唯一失败 `ChopLog.MatchesArchIndependentGolden`
  是 X87Fyl2x 的**同源 fldcw 笔误**的既有问题，stash 我的改动后仍失败 → 与本次无关，
  见下「遗留」）

**取证工具**：rmg_hook.dll 新增「树阶段取数日志」（进程内 hook，标志文件
`.qwen/tmp/_treedraw_on.flag` 门控安装，rmg_done 落盘 `<dump>.treedraw`）。
CLI 侧 TREEDRAW 探针（lat_spread.cpp，env 门控 RA2_RMG_TREES_TRACE）。
脚本 `.qwen/tmp/_mt1_treedraw_capture.py`（采集）、`_mt1_key_cmp.py`/
`_mt1_keydist.py`/`_mt1_keycmp2.py`（key 对拍）、`_mt1_seq.py`/`_mt1_between.py`
（交错序列）、`_key_exact.py`（精确复算）、`_kcode2_dis.py`（kCode2 反汇编）、
`_fastsqrt_pe.py`（FastSqrt 表值）。

### ⚠️ 遗留：X87Fyl2x 也有同源 fldcw 笔误（ChopLog 测试失败的真因）

`x87_fyl2x.h` 的 X87Fyl2x kCode 里，两处「fldcw」同样写成了 `0xDB`：
- `0xDB,0x6C,0x24,0x08`（fldcw [rsp+8] 设置，应为 D9）
- `0xDB,0x2C,0x24`（fldcw [rsp] 恢复，应为 D9）

→ ChopLog 也跑在最近舍入，与 golden（chop）差 1 ULP →
`ChopLog.MatchesArchIndependentGolden` 失败。**这是既有问题，与 SpreadKey 修复无关**
（stash 验证）。但 ChopLog 的 1 ULP 差会经 NormalDeviate → TruncNormal 传播，
可能还有未发现的影响面，应一并修（X87Fyl2x 的 DB→D9 同样修掉并回归）。

对拍工具：`.qwen/tmp/_rerun_and_cmp.py <seed> [case名子串过滤]`；
mt1 专项：`.qwen/tmp/_mt1_verify.py`（从 sweeps_cmp_12345.txt 提取发散案重跑）。
