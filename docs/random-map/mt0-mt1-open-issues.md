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

### mt1 单格树根因收窄（2026-10-08 活体取证）

代表案 `s12345_mt1_t0_th0_sz0_r3_p5`（发散格 (91,44)）与
`s12345_mt1_t0_th1_sz1_r0_p7`（发散格 (66,81)）。两格同型：
内部格、ground=0(Clear)、lv=4、ramp=0、无 overlay、u 远不贴 prob 边。

**活体实锤（断点 0x71BC7F = TerrainClass ctor 内 Unlimbo 失败路径）**：
该断点只在「树放置失败」时执行——全程 **0 命中**，且 live.bin 与真值
**逐字节一致**（0 diff）。⇒ 原版**从未调用 ctor** 种这棵树，是在
ctor 之前就拒绝了它，且取数净额与 CLI 完全一致（否则 rounds 3-99 会错位）。

**机制推论**：原版的 GrowTreePatch 在 u<prob 判定或更早的过滤/pop 上
就拒绝了该格，但消耗的取数与 CLI 的种植（u+idx 共 2 次）净额相同。
最可能的形态：round 内某处比 CLI **多推了一个堆节点**（+1 key 取数）
→ (91,44) 的 u 取数错一位变成 >= prob → 拒（省 idx 取数 -1）→ 净额 0。
多发的那一格来自更早 patch 的 flag47 残留差异（pop 序差 1 格导致
某格 flag47 状态不同）。

**已排除**：u<prob 浮点边界（u 离边很远）、RNG 流分叉（其余 211/212 树
逐格一致）、PickFrom1 拒绝差异（拒绝仅在 draw=0xFFFFFFFF）、可玩区边界
0x578540（格深在内部）、MoveCost[ground=0][zone]（全 190 棵树 ground=0）、
heap pop 序（该格全图只被推一次）。

**取证卡点**：Python 调试器挂 INT3 采逐次取数时游戏生成卡死（调试器挂上
后 gen 能点但生成不推进/极慢，多次复现；0x71BC7F 那次能完成是因为它
从未命中）。**可靠路径 = 给 rmg_hook.dll 加「树阶段取数日志」**（进程内
hook 0x5A47F2/0x5A4826/0x5A49B8，按返回地址过滤，写入大缓冲或落盘），
绕开调试器。需重建 DLL——**注意：采集期别动 rmg_hook.dll**（游戏已注入
旧版，换 DLL 会导致真值口径不一致），另建变体或等采集空窗。

**临时探针**：`code/rewrite/rmg/lat_spread.cpp` 的 TREEDRAW 加了 ground
字段（env 门控 RA2_RMG_TREES_TRACE，未提交）；CLI 侧脚本
`.qwen/tmp/_mt1_tree_probe.py` / `_mt1_case_probe.py` / `_mt1_draw_cmp.py`、
活体脚本 `_mt1_fail_capture.py`（0x71BC7F）/ `_mt1_draw_capture.py`（取数序列，
含单步 INT3 处理——修复了重武装死锁 bug）。

对拍工具：`.qwen/tmp/_rerun_and_cmp.py <seed> [case名子串过滤]`；
mt1 专项：`.qwen/tmp/_mt1_verify.py`（从 sweeps_cmp_12345.txt 提取发散案重跑）。
