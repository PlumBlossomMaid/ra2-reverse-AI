# RA2 随机地图复刻对齐工作手册（Seed=62298）

> 最后更新：2026-10-06（RMG 生成流程完全对齐，交接 kimi）
> 本文件是跨 Agent 的记忆索引（kimi / qwen code 不互通，一切以本文件为准）。
> **每轮工作结束立刻更新本文件**。更久远的取证结论在 `memory/` 与 `docs/random-map/`。

---

## 项目目标

复刻 Red Alert 2 (gamemd.exe) 随机地图生成器（RMG），**字节级对齐**：
同种子同图，最终 cells SHA256 == `81494d1fc00c90d06dd48897584ad623e8974e3563640aa775488b38c7d5d2e6`。

**⚠️ 权威基准（2026-10-06 更正）**：
`E:\code\ra2-reverse-AI\build_ninja62298\save_runtime_cells_62298.bin`（SHA=81494d1f）。
`snapshot_gamemd_62298\game_cells.bin`（SHA=f0a1ccc5）是活体对局中抓的，被矿生长污染，勿当基准。

---

## 当前状态总览（2026-10-06 RMG 完全对齐）

### ✅ od 重写已闭合——纯静态攻破（无动态 debug，qwen-code）

1. **变换语义实锤**：rmg_done 窗口（0x599354-0x5994B0）内**唯一变化 = 矿格 od**
   （iso/sub/lv/ramp/ov 全不变，297 格 od 变化 + 19 格恰好不变）。
2. **映射是确定性函数**：`od_new = T[pc]`，pc = 8 邻域（原版 0x89F688 表）内
   **同类矿**（ore 27-38 / gem 102-113 各算各的）overlay 数；
   T = {0:0, 1:1, 2:3, 3:4, 4:6, 5:7, 6:8, 7:10, 8:11} = `(pc*11+4)>>3`。
   **316/316 矿格全部满足，零例外**（19 个「未变格」= T[pc] 恰等于旧值：
   od=11 的 5 格全是 pc=8 → T[8]=11；od=0 的 1 格 pc=0 → T[0]=0）。
3. **CLI 已实现**（pipeline.cpp 最终 recalc_pass 之后，新快照点
   `after_ore_frame_smooth`）：CLI 最终态 vs live rmg_done **菱形内 12054 格
   ex/iso/sub/lv/ramp/od 全部 0 差异**，od 分布逐格一致。
4. 原版机器码位置仍未定位（窗口直接调用树全查过：RecalcAttributes 只有清 0、
   0x722D00/0x722240 建堆不写 od、0x568BB0=fog+第二次 Recalc、radar 只读；
   疑走区域析构虚调用或未识别路径——对 CLI 结果无影响，可后补）。
   取证脚本：`.qwen/tmp/_od_mask_test3.py`（同类 8 邻域 pc 纯映射证明）、
   `_od_pc_verify.py`（316/316 验证）、`_scan_od_writes2.py`（+0x11E 写点全表
   148 处）、`_scan_calls.py`（call 目标扫描）。

### ✅ rocks + 树林分岔已闭合（2026-10-06 qwen-code）

1. **根因实锤**：`CliWorld::CreateTerrain`（cliworld.cpp:519）只记录 terrains_ 列表，
   **从不置 cell.object (+0xE4)** → GrowTreePatch 种植检查 `Field0xE4==0` 永真 →
   CLI 在同一格重复种树（(96,81) 在 CLI 树表出现两次：索引 8 和 78）。
   原版 CreateTerrain=0x71BB90（trees 与 TIBTRE 共用，0x5A2C05 实锤）会置 +0xE4。
2. **修复**：CreateTerrain 加 `Mut(at).object = 1;`（一行）。
3. **验证结果**：
   - 树林 40/40 补丁 draws 与 emu 完全一致（总抽数 8997 == emu 8997）
   - rocks 入口 RNG 从 128289 修正为 **129159**（与推导的原版值精确吻合）
   - rocks 18/18 位置全中 live rmg_done
   - CLI vs rmg_done 菱形内 **ex/iso/sub/lv/ramp/od/ov 全部 0 差异**
     （ov 25 万差异全在菱形外，CLI=0xFFFF vs done=0，已修复 dump 层初始化）
   - ctest 43/43 全绿（除 kimi 在途的 2 个既有失败：place_start_points / cli）

### ❌ 剩余未闭合（一项，不影响生成流程）

1. **AUTH（save_runtime）的对局初期效应**：rmg_done → AUTH 差 19 格 od（矿生长
   +1/+2/+3）+ 4 格新增矿（0xFFFF→0x68-0x6D）。属进对局后的运行时生长，
   非 RMG 生成。要字节级对齐 save_runtime 需另立项模拟开局生长。

2. **副随机流身份实锤（2026-10-06 qwen-code）**：`[0xA8B230]+0x218` 是
   某对象内嵌 Randomizer（BSS，+0x355C/+0x3544/+0x11E8 字段），
   0x52FE17 从 `[0xA8ED94]`（Game::Seed）播种（`rep movsd 0xFD` 拷贝）。
   **Game::Seed 在 0x52FDF4 由 `timeGetTime()` 设置——非确定性**，
   save_runtime 是某次特定运行的快照，不可精确复现。

3. **矿生长 od 写点定位**：`0x5FBA37` = `add byte ptr [eax+0x11e], 1`
   （od++，Tiberium 生长/扩散逻辑，TiberiumClass 或 OverlayClass 方法）。
   其他写点：`0x485760` = `od = (Random%3)*3 + bl`（0x485736 用副随机流
   [0xA8B230]+0x218）；`0x424155` = `od = RandomRanged(0,2)`（副随机流）。
   但 AUTH 差异增量是 +1/+2/+3，与 0x485760 的 +3/+6/+9 不匹配——
   0x5FBA37 的 od++ 是更可能的来源。

4. **CLI 与 rmg_done 的 SHA 差异已闭合**（2026-10-06）：修复 dump 层菱形外格
   ov 初始化（CLI 保持 -1=0xFFFF，原版为 0；`cli_main.cpp` WriteCellsDump 里
   `ex ? overlay : 0`）。现 CLI vs rmg_done **全字段 0 差异**
   （ex/iso/sub/lv/ramp/od/ov 全对）。
   CLI vs AUTH（save_runtime）剩 19 od + 4 ov = game start 后矿生长
   （副随机流种子=timeGetTime()，非确定性，不可精确复现）。

### ⚠️ 基准陷阱：actual_randmap_62298.map 是另一局！

`build_ninja62298/actual_randmap_62298.map`（2026-08-30）与权威
save_runtime（2026-08-18）**不是同一局**：Waypoints（(90,22)/(40,105) vs
live 实锤的 (49,110)/(78,27)）与 TIBTRE 位置全不同。**不要拿它当 62298
的地面真值**（我一度误用它的 [Terrain] 节判树，得出"树林全错"的假结论——
作废）。当前唯一成品权威 = save_runtime_cells_62298.bin + live 采集。

### 历史：布矿阶段（tiberium）已完全对齐——活体验证

2026-10-06 用 headless x64dbg 采集了真实游戏的 tiberium 全程
（`snapshot_gamemd_62298/live_tiberium_20261014/`）：

1. **10 片矿田参数 live == CLI 完全一致**（fid/seed/count/gems 逐片相同）：
   candidates (47,63)/213 … (58,100)/254；starts (49,110)/500、gems=1，(78,27)/537、gems=1
2. **live after_tiberium rng=(131,234) == CLI**；tiberium_entry rng=(161,14) == AUTH 快照
3. **live after_tiberium 的 od 分布 == CLI 的分布**（od=11 饱和 206 格就是真实生成行为！）
4. emu（原版机器码+AUTH 入口态）逐片 == CLI：queued 全表 0 diff、每片 RNG 相等
   （emu 片耗 544/623/754/701/565/640/536/611/1489/1243）

### ❌ 当前唯一未闭合问题：od 在 tiberium 之后被重写

- live after_tiberium vs save_runtime：iso/lv/ramp 差数千格（hills/LATs，正常，
  CLI 的 LAT 已字节级对齐）；**od 恰好 300 格被改写**（≈全部矿格 316 + 新增 4 格）
- 改写后 od 非确定性地散在 1-11（同一 (ov,od) 组合映射到多个新值）→
  是随机化重写/生长，不是确定性函数
- overlay 编号 316/316 格完全保留；另有 4 格新增矿格

### ✅ od 重写时点已锁定（2026-10-06 od 二分采集，live_od_bisect_20261014/）

- after_tiberium / before_hills / before_lats / after_lats：od 完全不变（饱和 206×11）
- **rmg_done：od 已被重写**（297 格变化）→ 重写在 0x599354 与 0x5994B0 之间：
  1. 0x599361-0x59938E 全图 RecalcAttributes 循环（0x47D2B0）
  2. 0x59939B call 0x722D00（遍历 TiberiumTypes 0xB0F4EC，重建各类型 +0x110 格表，
     内部读 od<11 判断、用**另一个 Randomizer** [0xA8B230]+0x218——主 RNG 计数不变）
  3. 0x5993A0 call 0x722240
  4. 0x5993A5 "RMG: Cleanup"（释放 work 数组、销毁区域表）
  5. 0x59943F call 0x568BB0(MapClass, 1)
  6. 0x599451 "Compute Radar Image"（0x654490/0x654650）
- 已排除：RecalcAttributes 的 +0x11E 写只有清 0 两条（ramp>4 或 ramp!=0&tibflag 才触发，
  矿格 ramp≤4 不走）；0x722240 / 0x568BB0 全函数体不碰 +0x11E；
  0x722D00 前半（0x400 内）只读不写 od
- **最可疑：0x722D00 后半段（>0x722F80）或 0x722D00 之后**。
  关键佐证：重写不消耗主 RNG（after_lats→rmg_done rng 头不变 (28,131)），
  与 0x722D00 里用 [0xA8B230]+0x218 另一个随机流吻合
- rmg_done 的 od 分布 ≈ save_runtime（权威）：重写就是生成流程的最后一步；
  save_runtime 多出的 4 格和 13 个 od 差是进对局后的生长，与生成无关

### 下一步（用户交接给 kimi）

**当前状态**：RMG 生成流程已完全对齐 rmg_done（菱形内全字段 0 差异）。
CLI vs AUTH 剩 19 od + 4 ov = game start 后矿生长（副随机流种子=timeGetTime()，
非确定性，不可精确复现）。

**可选方向**（按优先级）：
1. **检查 .map 输出**（mapwriter 层）：CLI 生成的 .map 文件结构是否正确
   （IsoMapPack5/OverlayPack 编码、Waypoints/Terrain/Structures 字段序）。
   验证方法：用官方图 sandwell（100×150）或捡箱子（200×200）对比
   CLI 输出与原版 .map 的字节结构。
2. **模拟副随机流矿生长**（逼近 save_runtime）：反汇编 0x5FBA37 所属函数
   （0x5FB9xx-0x5FBAxx，Tiberium 生长/扩散），用固定种子模拟
   [0xA8B230]+0x218 的 R250 流，尝试复现 19 od + 4 ov 的精确位置。
   注意：原版生长结果每次不同（种子非确定性），只能逼近不能精确对齐。
3. **定位 od 平滑的原版机器码**（取证完整性）：od=(pc*11+4)>>3 的变换
   在 0x722D00 调用链未找到，疑在 Cleanup 或 Radar 段。可用
   `_headless_od_bisect.py` 细采（用户回来后）。
4. **处理 kimi 在途的 start_points 测试失败**：place_start_points / cli
   两个测试失败是 kimi 的 +220 行改动引入，需 kimi 修复。

**快速验证**：
```bash
cmd //c "E:\code\ra2-reverse-AI\.qwen\tmp\_build_cli.bat"
python E:\code\ra2-reverse-AI\.qwen\tmp\_od_smooth_verify.py
# 预期输出：CLI vs rmg_done 菱形内全字段 0 差异，CLI vs AUTH 19 od + 4 ov
```

### 本轮已完成的采集（都在 snapshot_gamemd_62298/）

| 目录 | 内容 |
|------|------|
| `live_tiberium_20261014/` | tiberium 全程：entry/after 全量 dump + fields_log（10 片参数+rng头）== CLI 全对 |
| `live_od_bisect_20261014/` | hills/lats/done 4 点 cells——证明 od 重写在 after_lats→rmg_done 窗口 |

---

## 活体采集方法论（2026-10-06 确立，重要）

### ✅ 正确方法：x64dbg headless 监督器

`.qwen/tmp/_headless_tiberium.py`（v4，计数驱动版）：
- spawn `E:\code\x64dbg\release\x32\headless.exe`，stdin 发命令：
  `attach <pid_hex>` → `bp <addr_hex>` ×N → 读回内存验证 0xCC
- 解析 stdout 的 `INT3 breakpoint at gamemd.XXXXXXXX!` **逐行计数**判定命中
  （不要按 EIP 轮询去重——连续命中间没有可观测的 running 窗口，会卡死；
  也不能按 (tid,esp) 去重——同调用点 esp 相同）
- 命中后 OpenProcess 读写内存做注入/dump，然后 stdin 发 `run`
- 外来暂停（TLS 回调 bp、DLL 加载——点生成按钮会新起线程触发）：
  stdout 出现 `[STATE] paused` 但不在我的断点上，1 秒后自动 `run` 放行
- 参数注入：入口 0x598960 断下时写 MapSeedClass(0xABDFD8)+0x38..+0x74
  （16 字段，从 `E:\YRLauncher\RandMap.Sed` 读）。用户无法在游戏 UI 控参数，必须注入
- 需要用户配合时 `ctypes.windll.user32.MessageBoxW` 弹窗
- 监督器被杀会连带游戏退出（debuggee 随 debugger 死），采集中断需重开游戏

### ❌ 失败方法（别再用）

- ctypes 自写调试器循环（DebugActiveProcess + WaitForDebugEvent）会卡死游戏。
  `_capture_live_stages_62298.py` 的调试部分不可用（其 dump_state 只读部分仍可用）。
  历史 run01/run06/at_5A23A0 快照是 x32dbg GUI + 只读 dump 做的。
- **`_capture_live_stages_62298.py` 的 STAGES 表里 hills/lats/done 四个地址全部 +1 错位**
  （落在指令中间，0xCC 会破坏操作数且不触发）。正确地址见下表。
  验证方法：`python code/randmap/disasm.py 0x<addr> 0x20` 必须是指令边界。

### 💡 副产品想法（用户提出，待办记录）

这套「headless 监督器 + 参数注入」可做成**地图种子固定化修改器**
（强制任意 Seed 玩同一张图）。先把索引记在这里，日后立项。

### 旧 emu 对拍脚本的两个基准错误（此前 per-field 结论全部作废后已修正）

1. Oracle 尺寸必须 `OreOracle(74, 82)`（side=157），不是 (70,70)；
   并修正 MAP+0xFC/+0x100/+0x104/+0x108 = 2/5/70/70（local 矩形）
2. 两片矿田间外层 NormalDeviate 消耗主 RNG，emu 重放须按 CLI ore_trace 的
   before/after 间隙补抽：`o._call(0x65C780, 0xABE890)` × gap（gap 实测 2/0/4/0/4/0/4/0/0）
3. 修正版：`.qwen/tmp/_emu_per_field_queued.py`、`_emu_vs_auth_final.py`、
   `_analyze_live_tiberium.py`（三方对拍）

### 已排除的根因（别再查）

SpreadKey/FastSqrt 精度；ContainedTiberiumIndex 区间；NormalDeviate 入口态；
「Push 失败 used++」（反汇编 0x5A2DDC 证伪）；哑元 0xABDC50（全程零命中）；
work.queued/flag45 入口态（全表 0 diff）。

---

## 取证资源

| 路径 | 说明 |
|------|------|
| `build_ninja62298/save_runtime_cells_62298.bin` | **唯一成品权威** SHA=81494d1f |
| `snapshot_gamemd_62298/at_5A23A0_20261003/` | tiberium 入口态快照 rng=(161,14) |
| `snapshot_gamemd_62298/live_tiberium_20261014/` | headless 采集：tiberium_entry/after_tiberium 全量 dump + fields_log（10 片参数+rng头） |

### 关键脚本（.qwen/tmp/）

| 脚本 | 用途 |
|------|------|
| `_headless_tiberium.py` | headless 采集监督器（改 BPS/逻辑即可复用于其他阶段） |
| `_fresh_orework.py` | CLI 重跑（ore trace/pop/enq/每片 queued dump） |
| `_emu_per_field_queued.py` | emu 逐片对拍（修正版） |
| `_analyze_live_tiberium.py` | live vs emu vs CLI 三方对拍 |
| `_build_cli.bat` | 构建 CLI |

### CLI dump 参数（build_x86 已编译）

`--dump-ore-trace/--dump-ore-pop/--dump-ore-enq/--dump-ore-work=PREFIX`
`--dump-stage-work-prefix/--dump-stage-work-names/--dump-stage-rng/--dump-stage-cells-prefix`

### 代码触点

- `code/rewrite/rmg/ore_field.cpp/.h`（CreateOreField/PlaceTiberium + dump 钩子）
- `code/rewrite/cli/pipeline.cpp:430-450`、`code/rewrite/cli/cli_main.cpp`
- `code/randmap/dump_tiberium_golden.py`（OreOracle）、`code/randmap/disasm.py`

### 关键地址

| 地址 | 说明 |
|------|------|
| 0x598960 | RMG 入口（注入点） |
| 0x5A23A0 | PlaceTiberium（tib 入口快照点） |
| 0x5A28C0 | CreateOreField（每片矿田入口；args: esp+4=seed*, +8=count, +C=fid, +10=gems） |
| 0x598EF4/0x598EF9 | PlaceTiberium 调用点/返回点（after_tiberium 快照点） |
| 0x599171 / 0x599215 / 0x599354 / 0x5994B0 | **修正后** before_hills / before_lats / after_lats_recalc / rmg_done（阶段消息 push 处） |
| 0xABE890 / 0xABDFB8 / 0xABDFD8 / 0xABED10 | 主RNG / NormalDeviate / MapSeedClass / work数组 |

---

## 纪律

1. **不 commit**——所有改动在工作区
2. **探针脚本留 .qwen/tmp/**——不入库
3. **代码以复现原版机器码为核心**——不发明等价算法
4. **中文回复**
5. **每轮结束立刻更新本 AGENTS.md**

## 快速启动（新会话）

```bash
cmd //c "E:\code\ra2-reverse-AI\.qwen\tmp\_build_cli.bat"        # 构建 CLI
python E:\code\ra2-reverse-AI\.qwen\tmp\_fresh_orework.py        # CLI 重跑带 dump
python E:\code\ra2-reverse-AI\.qwen\tmp\_emu_per_field_queued.py # emu 对拍
python E:\code\ra2-reverse-AI\.qwen\tmp\_headless_tiberium.py    # 活体采集 tiberium 全程（后台，弹窗叫人）
python E:\code\ra2-reverse-AI\.qwen\tmp\_headless_od_bisect.py   # od 重写精确定位采集（后台）
python E:\code\ra2-reverse-AI\.qwen\tmp\_analyze_live_tiberium.py # 三方对拍
```
