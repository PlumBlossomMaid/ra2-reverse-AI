# RA2 随机地图复刻对齐工作手册（Seed=62298）

> 最后更新：2026-10-07 晚（连生语义彻底实锤 + cleanregen 补丁 + 不动点机理）
> 本文件是跨 Agent 的记忆索引（kimi / qwen code 不互通，一切以本文件为准）。
> **每轮工作结束立刻更新本文件**。更久远的取证结论在 `memory/` 与 `docs/random-map/`。

---

## 2026-10-07 晚：连生（dirty-chain）语义实锤 —— 当前主线

### ✅ 游戏连生的完整语义（全部实证，非推测）

1. **进程首局（gen#1）= 唯一干净变体**：cells 全新分配（扩展字段全零）+
   0x599650(RandomMap::SetupTheater) 走完整重置子路径 0x599B1D（析构四对象
   数组 + 0x599E70 清格子循环：iso=0,sub=0,lv=4,ramp=0,ov=-1,od=0）。
2. **gen#2 起 = 不动点**：重置子路径被参数门控跳过（0x599A20 `jne 0x599B1D`
   的 arg 恒 0），cells 全字段（含 +0xE4/+0xEC/+0x4C/+0x10A/+0x113 等 0x150
   全部扩展字段）与对象数组（树/建筑/Anim/House）原样带入下一局。
   实测 seed1：g1≠g2（7021 格），**g2==g3==g4 逐字节相等（不动点）**。
3. **不动点历史无关**：T0/T1/T0/T1 交叉生成，同参两次输出逐字节一致。
   且 **Time 参数对 cells 无影响**（T0==T1，Time 只影响光照/地图头）。
4. **work 数组每局重置**：0x59A2E8-0x59A378 每局 free+new 并全零化
   （+0x38=-1、+0x42=1）——CLI 的 work 重置行为正确。
5. **gen#1 vs 不动点的差异源 = 12B 之外的扩展字段**（gen#1 它们为零，
   gen#2+ 为脏）。正在逐字段二分定位决定性字段集（_zero_ext_fields.py）。
6. 真实玩家几乎永远玩到不动点变体（首局是进程级一次性异常）——
   **CLI 脏链模式（复现不动点）是主交付物**，干净模式（gen#1）已对齐。

### ✅ cleanregen 补丁（已验证，已 commit 4050749）

- 两个补丁强制每局走原版完整重置路径：0x599A20 `jne 0x599B1D`→`jmp`、
  0x599BD3 `mov [esp+0xf],bl`→NOP（flag 恒 1）。
- 效果：c1==c2==c3==自然不动点（g2/g3）0 差异；新进程 gen#1+cleanregen
  == 自然干净首局 0 差异。**8960 案穷举无需链语义，任意顺序采集即可**。
- rmgctl `cleanregen on/off`（OpenJob(true) 可 inject 前预置）。

### ✅ 修改器 MASM 陷阱修复（重大 bug）

`HookTibAfter` 里 `mov eax, dword ptr [0x82AF14]` 被 MASM 编成
`B8 imm32`（[imm] 无寄存器基址时被当成立即数）→ 游戏 `call eax` 跳进
.data 执行数据（崩于 0x82AF1F）。**naked asm 里绝对地址必须写
`mov eax, dword ptr ds:[82AF14h]`**。之前两轮"游戏莫名崩溃"全是它。

### ⚠️ 闹鬼事件（已解决）

后台子代理 **agent-4（mt0）超时后 loop 不死**（同进程协程，任务系统
杀不掉），2 小时内批量弹出十余个"取证请求"MessageBox。**教训：
子代理超时 ≠ loop 终止；它启动的 `Start-Process` 分离进程更难缠**。
最终靠用户重启 kimi code 才杀掉（wire 出现 `agent.turn.ended: aborted`）。
**弹窗一律当闹鬼处理：沿 ParentProcessId 链查到哪个 node，再查该
node 的 agents/*/wire.jsonl 是否还在增长。**

### CLI 双模式目标（用户拍板）

1. 干净模式：复现进程首局（已对齐 ✅）。
2. 脏链模式：复现不动点（**当前攻关**）。CLI chain/replay 现在能灌 12B
   脏态，但与不动点差 ~8.7K 格——缺扩展字段的建模。正用
   `_zero_ext_fields.py`（活体清零指定 offset + gen）二分决定性字段。
   注意：ov/iso/zoneId/ground 按 **dword** 保留（游戏按 int32 读 ov，
   清错高半字 → 0xFFFF 索引进 OverlayType 表崩溃 0x47D30F）；
   全清会波及渲染链（ra2md.dll+0x2BAA ddraw 崩溃，同 pristine 判死点）。
   只能清直方图指控的标量字段：0x100/0x10A/0x10C/0x10E/0x113-116/
   0x122/0x124/0x140/0xDD。

### ✅ 干净连生采集模式定型（k 系列实验，全部实证）

1. **开关 = [MapSeed+0x178]**（上一局参数快照指针，0x178 字节 MapSeedClass
   拷贝）：点击生成**之前**外部清零 → 该局输出 == 进程首局干净变体
   （z4/k1-k3 全部逐字节 == g1）。注意：**在 RMG 入口清（hook 内）是错误
   位置**——handler 的 delete+alloc 已发生，会产出第三变体（z6-z8，
   格子表延迟分配只有 3539 格）。已在 dllmain.cpp 注释中记录，勿再犯。
2. 采集链路：`_game_session.py` 的 `gen_clean()`（先清 +0x178 再 rmgctl gen）。
   `ensure_game` 幂等（已注入跳过 inject）。
3. **CLI 干净批量**：`ra2_rmg_cli --chain=X --fresh-per-game`——每局全新
   CliWorld == 独立单局（干净变体历史无关已实锤），~2s/案。
4. **gen#1 与不动点的差异源**：不是 cells 扩展标量（全区域清零无效）、
   不是副随机流（钉流无效）、不是对象（析构后输出不变）——只剩
   +0x178 存在性相关的行为差异（待后续挖，脏链模式复现不动点时需要）。
5. **Time 参数对 cells 无影响**（T0==T1 逐字节），4 个 Time 值只影响地图头。

### 🚀 8960 案大穷举执行中（2026-10-07 午后）

- 游戏侧：`_clean_sweep.py 12345` → `sweeps/12345/<case>.bin`（~3.2s/案，
  崩溃自动重启续跑，progress.log）。
- CLI 侧：`_cli_sweep.py <seed> <th>`（每 th 10 块 × 224 案，
  --fresh-per-game）→ `sweeps_cli/<seed>/th<T>_c<CC>_<II>.bin`。
- 对拍：`_sweep_cmp.py 12345` → `sweeps_cmp_12345.txt`。
- **首轮（mt0 全部 234 案）**：158 pass / 76 diff——全部集中在
  th0_sz0（温带小图）的 p2/p3/p5/p7/p8，每案 ~845 格（iso/ov/od/gnd），
  y 52-124 矿田放置不同。th1 雪地 112/112 全过。
  agent-5（coder）正在修，方向=Recalc#1 zone 输入侧（agent-3 取证）。
- dll 被锁（僵尸 gamemd 持有旧 dll）：**改名规避**（Windows 允许对有
  活跃映射的文件 rename）——`os.rename` 后即可重新链接。

### 工具链新增

- `.qwen/tmp/_game_session.py`：ensure_game()/gen_to()——起游戏、导航、
  cleanregen、inject、arm、gen 一条龙（全 PostMessage，无弹窗）。
- `.qwen/tmp/_zero_ext_fields.py`：活体按 offset 清格子字段+gen+ dump。
- `E:\code\RA2YR_ReSource`（克隆）：他人全二进制重建项目，只有函数名/
  类布局有价值（RandomMap::Generate=0x598960、SetupTheater=0x599650 等），
  RMG 无实现，我们远超。真实实现仅 472/19067 函数（2.5%），exe 能链接
  不能跑（810 个 return-0 桩）。已停更一个月。

---

## 2026-10-07 修改器（rmg_hook.dll + rmgctl）——当前主线

### ✅ 全链路已打通（trainer 模式，与游戏死活隔离）

- `code/modifier/rmg_hook/dllmain.cpp`：0x598960 入口 hook 注入 16 参数到
  MapSeedClass(0xABDFD8)+0x38..+0x74；0x5994B0 rmg_done hook 进程内 dump
  cells（9B/格）。**DLL 自持共享内存 `Local\RMGHookJob2`（固定 64KB，换名
  防旧泄漏映射）和事件 `Local\RMGHookUnload`**——注入方退出后控制面仍存活；
  eject 恢复原始字节、CloseHandle、FreeLibrary，游戏无感。
- `code/modifier/rmgctl/rmgctl.cpp`：inject/arm --sed/disarm/setdump/gen/
  click [--real]/waitdump/randlog/fixstream/pristine/status/eject。
  `build.bat` 全量构建（需游戏未加载 dll），`build_rmgctl.bat` 只构建前端。
- ⚠️ **DLL 必须检查 cmd==1 才注入**（曾经不检查：disarm 后仍写入全零参数，
  生成死循环卡死游戏——已修）。

### ✅ 关键发现：菜单是真实 Win32 对话框树，BM_CLICK 全后台可用

- 游戏菜单页是叠放的多个真实 #32770 对话框，EnumChildWindows 可达全部控件。
- **「预览地图」= 控制 ID 0x0620，BM_CLICK 即触发生成**（无模态弹窗，可连续点）。
  「替我挑选」0x0621 只随机化对话框参数不生成。控件 hwnd 每局重建，ID 稳定，
  必须每次现找（取可见+可用者）。游戏不用前台、用户随便动鼠标。
- 纯后台链路：inject → arm → gen → waitdump，全程不碰真实鼠标。

### ✅✅ 重大结论：RMG 变体之谜（矩阵验证「CLI 分岔」真相）

**RMG 输出 = F(16 参数, 生成入口时的格子残留状态)**：

1. 主 RNG 每局入口由 `Randomizer::Seed(MapSeed.Seed)`（0x598985）重播种，
   栈对象 rep movsd 拷进 0xABE890——**主随机流完全确定**。
2. 副随机流 [0xA8B230]+0x218 每局仅被 BuildingClass ctor（0x43B750 内
   0x6F3259）抽 2-4 次写 +0x3C8，**不影响地形**（fixstream 实验曾误判
   因果，实为残留不动点的巧合）。
3. **真正的变体源：上一局地图的格子残留**。开机后格子表（0x87F924）
   全 NULL；首局生成后格子常驻。后续生成的放置/重试逻辑读旧格内容
   （占用 +0xE4、overlay、iso…）→ 重试次数变 → 主流位置偏移 →
   科技建筑数量/位置变 → 全图变体。同参数同会话：首局=变体A，次局起=变体B。
4. **CLI（零初始状态）== 开机首局**（seed1、seed12345 实测字节级）。
   矩阵验证的 13 个「CLI 分岔」用例全是会话中段（脏状态）采集的假象；
   9/22 稳定 MATCH 是对残留不敏感的用例。
5. 阶段 7 科技建筑计数实测：`m = PickBelow(5)`（0x5A9645 用主流 0xABE890），
   seed1 脏局 m=4（ctor@219/222/225/239），干净首局 ctor 只有 225/239。

### ✅ CLI 回放模式：--init-cells / --dump-cells12（2026-10-07）

脏局复现链路打通（算法零改动，只改初始状态）：

- `CliWorld::InitCells()`（cliworld.cpp）对齐原版阶段 1：删掉多清的
  object(+0xE4)/ground(+0xEC)/zoneId(+0x4C) 三行——原版只重置 6 字段。
  全新进程这三个字段默认就是 0，首局行为不变（ctest 43/43 全绿实证）。
- 新参数 `--init-cells=FILE`：pipeline 在 CliWorld 构造后、**InitCells 前**
  调 `CliWorld::LoadInitCells12` 灌入上一局终态，InitCells 随后按原版语义
  重置 6 字段、保留 object/ground/zoneId 残留 → 脏局。装载语义：
  12B/格 × 0x40000 槽（[0]=ex [1..2]=isoTile i16LE [3]=sub [4]=lv [5]=ramp
  [6]=od [7..8]=overlay i16LE(-1=无) [9]=object 占用布尔 [10]=ground 低字节
  [11]=zoneId）；rec[0]==0 或本世界不存在的槽跳过。
- 新参数 `--dump-cells12=FILE`：同格式导出终态（object 写占用布尔），
  供链式回放。自链验证（seed1.sed）：run1→run2→run3 全跑完，逐局不同
  （terrains 365/327/286 = 脏局效应），且无 --init-cells 的首局输出
  逐字节不变（SHA 一致）。
- 触点：cliworld.h/.cpp、pipeline.h/.cpp（PipelineResult.init_cells_path）、
  cli_main.cpp（WriteCells12Dump + 参数解析）。

### 待办

- **真·CLI bug 已确认 3 例**（2026-10-07 重启真首局验证）：theater1 差 10 字节、
  mt0 差 1847、mt1 差 28379——三种测量语境（脏扫/pristine/真首局）下差异
  逐字节一致，与残留无关，是 MapType 0/1 分支、Theater 1 的算法级分歧，
  待修。seed1/seed12345 真首局 MATCH（残留假象类已澄清）。
  剩余 9 案（mt2/res1/res2/water60/time1/combo_a/combo_b/seedmax/theater2）
  待按同样标准终审。
- **会话内重置已判死**（2026-10-07）：官方析构 `Scenario_ClearClasses`
  （0x6851F0，"Clearing old scenario"，void cdecl 无参）两条调用路径都崩——
  ①RMG 入口 hook 内直调：c000041d 回调内致命异常（在 BN_CLICK 处理器嵌套
  里拆对象）；②WndProc 子类化 + 自定义消息 WM_APP+0x42 在消息边界调：
  重置本身成功返回，但**下一局生成崩**——ClearClasses 把 House 数组里
  RTTI==0x12 的项摘掉（含 Neutral），阶段 7 FindHouseByType("Neutral")
  拿不到 owner，RMG 缺了它需要的对象。官方流程靠 ReadScenario 重建，
  单独调不可行。手工清格子/字段也都崩。**结论：游戏就让它自然脏着，
  对齐走 CLI 脏链回放（用户拍板：原版 cells 本来就是脏的，脏链对齐
  才是真对齐）。**
- **CLI 进程内连生 --chain 已落地**（2026-10-07，agent-1）：`--chain=FILE`
  （每行一 .sed，空行/# 跳过），RunPipelineChain 共享一个 CliWorld/work/rng
  顺序跑局（RunPipeline 拆出文件内 RunGameOnWorld 单局体，单局行为不变，
  ctest 43/43 绿）；每局入口 `rng.Seed(seed)`（Seed 完整复位==原版重播种）。
  链内尺寸/theater 不一致 → 报错。--out=输出目录（默认 rmcache）逐局写
  `NN_<sed基名>.map`；--dump-cells12 逐局命名（%02d 替换，否则加 _NN）。
  与 --init-cells 互斥。验证：链 #01 == 单跑 seed1 逐字节（14f6ef9c…），
  #02 ≠ #01（脏局效应）且重跑逐字节确定；连生 #02 vs 磁盘回放 #02 差
  6824 格（object 字节 664 格 = 树占用残留保住）。

### ✅ +0xEC(ground)/+0x4C(zoneId) 两列对齐（2026-10-07，agent-1）

首局 12B vs 游戏 `replay_chain2/00_seed1_game.bin`：**全列 0 差异**
（修复前差 gnd 30 + zone 297；9B SHA acc0f2df… 不变，ctest 43/43）。
反汇编实锤的机制（旧 CLI 三处错）：

1. **0x47D318：overlay != -1 → +0xEC = OverlayTypeClass+0x298 无条件写**
   （旧版只对矿写）。+0x298∈{4,9}(Wall/Railroad) 或 +0x2AC≠0 → overlay
   专属路径 0x47D333（不经 tile 查表/空洞清理；CliffBack 变体命中且
   Rules+0x664==2 → 无条件 ground=3）。+0x2AC 的 INI 口径 =
   `Tiberium=yes || NoUseTileLandType=true`（SROCK/TROCK/VEINHOLE 均有）。
   **矿石区间 27-38/102-113 恒 {land=5, 占地}，优先于 INI 表**——mod
   rulesmd 把 27=NAWALL、102-104=LOBRDG26-28 重排过，但活体这些格
   ground=5/zone=0（与 ContainedTiberiumIndex 既有取证一致）。
2. **overlay 路径直达 RecalcLandType（0x47D551）**——旧版 ground==5
   早退跳过是错的（0x47CA80 全文无 +0xEC 写，已扫）。
3. **0x483C80 全分支**（zones.cpp RecalcLandTypeFull，旧版是阶段 6 子集）：
   出图→7；overlay +0x22D→1/+0x2A8→2/Ground[+0x298].Foot==0.0f→6/
   +0x2B5→6/+0x2B4→0（+0x2B4==0 贯穿）；+0xEC==2→4/==6→3/
   Foot≤0.01→6；+0xE4 占据者循环：**RTTI 0x24 树 →
   TemperateOccupationBits==7（雪地查 Snow，Scenario+0x1258==1）→
   zone 2，否则 5**；其他 RTTI 跳过→0。数据实锤：zone=2 的 30 格 =
   TREE04/08（TemperateOccupationBits=7），238 格普通树 = 5，8 格
   TIBTRE-on-ore = 2（TIBTRE 无 INI 键 → 缺省 7），建筑格 = 0。
   overlay 四标志（+0x22D/+0x2A8/+0x2B5/+0x2B4）RMG 无实例，按 0 建模。
- overlay 170 之名：`170=USELESS; ... VEINHOLE` 行尾词解析到 [VEINHOLE]
  （Land=Rock + NoUseTileLandType=true，与活体 (42,47) 完全吻合）。
- 数据链路：rules_loader 解析 Land=/Tiberium=/NoUseTileLandType= +
  TemperateOccupationBits=/SnowOccupationBits= → RmgRules.overlay_land /
  terrain_occ → CliWorld 注入；CreateTerrain 登记 terrain_by_cell_。
- **判定实验（三关）**：关1 ctest 43/43 + 9B SHA 不变 ✓；关2 首局 12B
  全列 0 差异 ✓；顺带 msg_g1 == chain#01 逐字节 ✓。
  **关3 未过：chain#02 vs msg_g2 差 6655 格（iso 5996 / lv 2615 /
  ramp 3224 / od 665 / ov 691，ex/sub 0）**。结构对比：游戏脏 delta
  6835 vs CLI 脏 delta 6930、相交 6053、交集中值相同仅 1057 →
  两链在同一起跑线发散但随机流早期错位。已排除：g1 12B 残留（全对）、
  NormalDeviate 入口重置（0x5989A6 rep movsd 清零 ✓）、MapSeed
  +0x304/+0x308（每局 cleanup 清零 ✓）、树名丢失（chain vs 磁盘回放
  仍差 7265 格 = 内存链更忠实，非发散源）。**剩余嫌疑（需活体裁决）：
  ① CellClass 未建模字段的跨局残留（真实格 ~0x170B，阶段 1 只清 6
  字段，若有生成逻辑读 +0xF8/+0x118 等残留 CLI 无法复现）；
  ② rmg_done → g2 入口间菜单循环对格子的运行时改写（TIBTRE
  SpawnsTiberium 等）；③ work 数组/其他全局的原始清理范围与 CLI
  InitCells 全清不一致。建议下一步：headless 抓 g2 入口/after_seeding_
  water 的 12B dump 与 CLI chain g2 同点对拍，直接定位首个分叉阶段。**
- 之后：大全量穷举（2 种子 × UI 六维参数），游戏侧脏链连续生成 +
  CLI 同链连生，逐局对拍；断链发散即真 bug，定位修。
- rmgctl sweep 子命令；C# GUI 未启动
- 矿生长不进 CLI（用户拍板：画蛇添足，超出复刻生成算法的目标）
- **纪律重申：CLI 只复刻原版机器码语义，不发明等价算法；
  挂钩子/清零只是取证手段，绝不进 CLI**

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
1. ~~**检查 .map 输出**~~（2026-10-06 闭合）：结构（节序/编码/键值格式）与
   游戏捕获逐项一致；内容与 live 同会话真值全对（Terrain 374/374、
   Overlay 0 差异、IsoMapPack5 12054 格 0 差异、Waypoints/Structures 锚点一致）。
   详见 docs/random-map/map-output-format.md。产物 `E:\YRLauncher\rmg62298_cli.yrm`。
   唯一遗留：[Structures] 字段值（owner "0" vs "Neutral" 等）需 MapType=3
   活体 Save Map 裁决——属游戏行为验证，非格式问题。
2. **模拟副随机流矿生长**（逼近 save_runtime）：反汇编 0x5FBA37 所属函数
   （0x5FB9xx-0x5FBAxx，Tiberium 生长/扩散），用固定种子模拟
   [0xA8B230]+0x218 的 R250 流，尝试复现 19 od + 4 ov 的精确位置。
   注意：原版生长结果每次不同（种子非确定性），只能逼近不能精确对齐。
3. ~~**定位 od 平滑的原版机器码**~~（2026-10-06 全链闭合）：
   - **调用点** = `0x59944C call 0x568BB0(MapClass, 1)`（rmg_done 窗口内；
     断点二分实锤：0x59943F 处 od 未变 → 0x599451 处已变）
   - **写点** = `0x4819D8 mov byte ptr [esi+0x11e], al`，所在函数 **0x4818E0**
     （0x568BB0 逐格循环在 0x568DD8 对矿格调用）
   - **机制**：数 8 邻域同 TiberiumIndex 格数（0x5FDD20 比对）→
     `idiv [type+0xE4]` 取余 → od = **dword 表 [0x81CD28+余数*4] 低字节**。
     表前 9 项 {0,1,3,4,6,7,8,10,11} 与活体拟合的 (pc*11+4)>>3 完全一致
   - 早期扫描未命中的原因：按连续**字节**搜表，实际表存 dword
   - 另：0x4818E0 有第二分支（[esp+0x1c]≠0 时用副随机流随机化 overlay id，
     RMG 窗口未触发，316/316 overlay 编号保留佐证）
   - CLI 已改为字面表查表（pipeline.cpp after_ore_frame_smooth），输出不变
   - 采集：snapshot_gamemd_62298/live_od_watch/（硬件监视点+断点二分）

### ✅ 副随机流确定性化 + 对局生长逐字节复现（2026-10-06 晚，live_seed_hook/）

**用户提议的 hook timeGetTime() 已验证可行，且用了更干净的等价做法**：

- 播种链：timeGetTime() → [0xA8ED94](Game::Seed) → Randomizer::Seed ×2 →
  rep movsd → [ScenarioClass]+0x218 与 0x886B88 两条流；**进程启动时播种
  一次**（0x52FDxx 只跑一次，开局 bp 抓不到）
- 等价方案：**rmg_done 断点处直写固定 R250 流状态**（[ScenarioClass]+0x218
  的 +4 起 1008 字节：i1=0/i2=103/state[250]，+0 虚表指针不动）——
  不依赖播种时序，效果相同。固定流 blob：
  `live_seed_hook/fixed_stream.bin`（seed=0x5EED0001）
- **验证（决定性）**：两局 62298（注入参数 + 固定流）无操作跑到 602 帧，
  cells_9b 与主 RNG 状态**全部 0 差异**（SHA 633072fd…相同）——
  对局全程（矿生长+AI+一切副随机流消费）完全确定
- run1 vs rmg_done：iso/sub/lv/ramp 0 差异，od 17 + ov 10 = 602 帧的
  矿生长（od+1 帧增长、新宝石 od=3、od=3 无 overlay 的累加格），
  与 0x722440 机制完全吻合
- **意义**：CLI 生长模拟有了确定性基准（fixed_stream + cells_run1/run2），
  可把 0x722440/0x7221B0 移植进 CLI 后逐帧对拍到任意帧数
- 工具：`.qwen/tmp/_headless_det_run.py`（弹窗叫人会话；教训：
  x64dbg 断点库会跨会话恢复残留断点需 bc 清理（已删
  `x32/db/gamemd.exe.dd32`）；强杀调试器会在游戏内存留 0xCC 导致
  无调试器时执行到即崩，attach 后须先恢复原字节）

### ✅ 内部状态全量对拍（2026-10-06，kimi，脚本 .qwen/tmp/_stage_audit_full.py）

CLI 逐阶段内部状态 vs 活体同会话（2026-10-14 系列采集）：

| 层次 | 验证点 | 结果 |
|------|--------|------|
| cells 全字段 | 6 点：tiberium_entry / after_tiberium / before_hills / before_lats / after_lats / rmg_done | **0 差异** |
| RNG 位置 | 每点 index1/index2 | 全吻合（161/14 · 131/234 · 131/234 · 195/48 · 28/131 · 28/131） |
| RNG 全状态 | rmg_done 的 state[250]（0xC 起 1000B） | **逐字节 0 差异**（唯一差 +0 虚表指针 0x7E8500，非随机状态；布局实锤 {vptr, index1, index2, state[250]}=0x3F4） |
| work 数组 | tiberium_entry / after_tiberium：24649×0x50B | 仅 **8 格 mask_cache(+0x40)** 差 1（live 63/58/50… vs CLI 62/…），其余 1971912 字节全等 |

- 原版 Randomizer @0xABE890 布局更正：+0=**虚表指针**（不是 disabled 标志），
  +4=index1、+8=index2、+0xC 起 state[250]。CLI `--dump-rng` 终态可与之
  直接对拍。
- 布矿返回后 work +0x38/+0x3C 的整表清理位置无矛盾：活体采集点 0x598EF9
  正落在清理**之前**，CLI 清理在其后——两侧时序一致。
- ~~**唯一未闭合的内部残差**~~：8 格 mask_cache(+0x40) 岸线掩码缓存错位——
  **已闭合（2026-10-06 晚，live 掩码写历史对拍）**。根因：原版 ShoreMask
  (0x57B210) 直接查格指针表，指针为空的邻格用 self_is_water 顶替、
  不读哑元；CLI 误走 CliWorld::IsoTile 的哑元回退。修复后 work 数组
  与活体两点逐字节 0 差异。**至此内部状态零已知残差**。
  采集：snapshot_gamemd_62298/live_mask_trace/（4 断点写历史 27658 条）。

### ✅ .map 键编码转置修复（2026-10-06，用户 FA2 目检驱动）

**现象**：树长在水里、出生点/科技建筑错位；矿和石头（overlay）正确。
**根因**：CLI 写 `x*1000+y`，正确是 **`y*1000+x`（y 在千位）**。
**实锤链**：
- 游戏读 [Waypoints] 的唯一 ÷1000 站点 `0x68BE0C`：V%1000→CellStruct
  低 16 位、V/1000→高 16 位；`GetCellAt`(0x5657A0) 低=X 高=Y →
  键=Y*1000+X
- 存档出生点存值 110049/27078 ↔ live (49,110)/(78,27)
- FA2 写链 MapData.cpp:1775-1787（key=itoa(x)+itoa(y 补 3 位），
  FA2 X 轴=游戏 Y 轴）
- 2026-08-15 的「x*1000+y 实锤」= 与自建脚本的循环论证，作废
**修复**（mapwriter.cpp KeyXY → y*1000+x；Structures 槽 3/4 落游戏 x/y；
test_cli 期望刷新）：waypoints `0=110049/1=27078`、矿树 `63047=TIBTRE02`
↔ live 矿田中心 (47,63)。ctest 43/43 全绿，产物 `E:\YRLauncher\rmg62298_cli.yrm`。

### ✅ kimi 在途测试失败已闭合（2026-10-06 kimi，commit 83f0f2d）

**根因**：146421d 把 0x594876 的矩形收缩移进 PickPointsInRegion 内部
（对齐原版机器码，golden harness 0x87F8E4..F0={0,0,w,h} 实锤），但
test_place_start_points 仍传**预缩过的** usable → 双重收缩；同时
AreaIsPassable 的菱形判定从 MapSeedClass 的 map_h 现算 +12，与 golden
harness 的 [0xABED08]=w+2h 不一致。test_cli 的期望值（61901/49）则是
内缩改动前的实测，提交时就没跑过。

**修复**：
- StartPointContext 补 `map_rect_h`（MapClass +0xF8=H+12）字段，
  AreaIsPassable 菱形判定改算 map_rect_w + 2*map_rect_h
  （=[0xABED08] 的真实来源 0x5662FB `lea eax,[eax+edx*2]`）；
  pipeline 补 `sp.map_rect_h = ms.height + 12`（CLI 行为不变）
- test_place_start_points：ctx.usable 改传未缩 {0,0,w,h} + map_rect_h=c.h
- test_cli StartPointsUseInsetLocalRect：期望值刷新为内缩落地后的
  实测 61928/76（region=0/start_points=2 不变）

**验证**：ctest 43/43 全绿；CLI vs rmg_done 复验菱形内全字段 0 差异
（AUTH 剩 19 od + 4 ov 不变）。

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
