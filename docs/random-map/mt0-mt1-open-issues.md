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

### 剩余 ~772 格 = 布矿发散（sz0 紧岛屿地理）

矿 overlay 位置不同。已排除（全部实证，**别再查**）：

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

**判定**：布矿入口状态在一切可 dump 维度都与 CLI 一致，但游戏输出不同；
p4/sz1 同法完美复现证明 harness 可信。**破局需要游戏侧布矿入口真身状态活体采集。**

### 所需活体采集（x64dbg headless，案 = s12345_mt0_t0_th0_sz0_r0_p2，cleanregen）

- bp `0x598EF2`（call 0x5A23A0 前）：dump 0xABE890(0x3F4 RNG 全状态)、
  0xABDFB8(0x18 ND)、0xABDFD8(0x360 MapSeed)、cells 12B、work([0xABED10]
  side²×0x50, side=157)、区域向量 [0xABDFA0]/[0xABDF94]+各区域对象及候选/格子向量、
  waypoints [0xA8B230]+0x632×8、哑元 0xABDC50(0x170)
- bp `0x598EF9`（布矿返回）：cells 12B + work（隔离布矿与下游）
- ⚠️ BP 地址必须先用 `python code/randmap/disasm.py 0x<addr> 0x20` 验证是指令边界
- 工具链就绪：`.qwen/tmp/_mt0fix_delta2.py`（入口态对拍）、`_p3_selfcheck.py`（同法任意案）
- **等游戏空闲再做**（游戏被 12345 批量采集占用时别抢）

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
- **sz0_r*_p8 ×24（新确认的大发散组）：完全未变**——th0 恒 4517 格
  （iso_lo:3890,iso_hi:2092,gnd:1829,ramp:1812,lv:1707）、th1 恒 3525 格；
  与 t、r 均无关（同 p8 下逐案逐格一致），差异从早期阶段就定型，与本次修复无关。
  **这是 mt1 当前最大开放问题**：p8 + sz0（最小图 8 玩家紧岛屿）在岛屿/出生点
  布局阶段整体分岔，需要与 mt0 同法的活体入口态采集（bp 0x598EF2 区）。

对拍工具：`.qwen/tmp/_rerun_and_cmp.py <seed> [case名子串过滤]`；
mt1 专项：`.qwen/tmp/_mt1_verify.py`（从 sweeps_cmp_12345.txt 提取发散案重跑）。
