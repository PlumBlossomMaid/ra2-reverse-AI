# 游戏侧活体采集与修改器工具链

> 来源：原 AGENTS.md（kimi-k3，2026-10-06/07）。本文固化"怎么从活游戏里取真值"的全套方法。

## 修改器：rmg_hook.dll + rmgctl

- `code/modifier/rmg_hook/dllmain.cpp`：0x598960 入口 hook 注入 16 参数到
  MapSeedClass(0xABDFD8)+0x38..+0x74；0x5994B0 rmg_done hook 进程内 dump cells（9B/格）。
  **DLL 自持共享内存 `Local\RMGHookJob2`（固定 64KB，换名防旧泄漏映射）和事件
  `Local\RMGHookUnload`**——注入方退出后控制面仍存活；eject 恢复原始字节、
  CloseHandle、FreeLibrary，游戏无感。
- `code/modifier/rmgctl/rmgctl.cpp`：inject/arm --sed/disarm/setdump/gen/
  click [--real]/waitdump/randlog/fixstream/pristine/status/eject。
  `build.bat` 全量构建（需游戏未加载 dll），`build_rmgctl.bat` 只构建前端。
- ⚠️ **DLL 必须检查 cmd==1 才注入**（曾经不检查：disarm 后仍写入全零参数，
  生成死循环卡死游戏——已修）。

### MASM 陷阱（重大 bug，已修）

`HookTibAfter` 里 `mov eax, dword ptr [0x82AF14]` 被 MASM 编成 `B8 imm32`
（[imm] 无寄存器基址时被当成立即数）→ 游戏 `call eax` 跳进 .data 执行数据。
**naked asm 里绝对地址必须写 `mov eax, dword ptr ds:[82AF14h]`**。

## 游戏菜单是真实 Win32 对话框树，BM_CLICK 全后台可用

- 菜单页是叠放的多个真实 #32770 对话框，EnumChildWindows 可达全部控件。
- **「预览地图」= 控件 ID 0x0620，BM_CLICK 即触发生成**（无模态弹窗，可连续点）。
  「替我挑选」0x0621 只随机化对话框参数不生成。控件 hwnd 每局重建，ID 稳定，
  必须每次现找（取可见+可用者）。游戏不用前台、用户随便动鼠标。
- 纯后台链路：inject → arm → gen → waitdump，全程不碰真实鼠标。

## 连生（dirty-chain）语义——全部实证

1. **进程首局（gen#1）= 唯一干净变体**：cells 全新分配（扩展字段全零）+
   0x599650(RandomMap::SetupTheater) 走完整重置子路径 0x599B1D（析构四对象数组 +
   0x599E70 清格子循环：iso=0,sub=0,lv=4,ramp=0,ov=-1,od=0）。
2. **gen#2 起 = 不动点**：重置子路径被参数门控跳过（0x599A20 `jne 0x599B1D`
   的 arg 恒 0），cells 全字段（0x150 全部扩展字段）与对象数组原样带入下一局。
   实测 seed1：g1≠g2（7021 格），**g2==g3==g4 逐字节相等（不动点）**。
3. **不动点历史无关**：交叉参数生成，同参两次输出逐字节一致。
   **Time 参数对 cells 无影响**（只影响光照/地图头）。
4. **work 数组每局重置**：0x59A2E8-0x59A378 每局 free+new 并全零化
   （+0x38=-1、+0x42=1）。
5. **gen#1 vs 不动点的差异源 = 12B 之外的扩展字段**（gen#1 为零，gen#2+ 为脏）。
   逐字段二分脚本 `_zero_ext_fields.py`（活体清零指定 offset + gen）。
   注意：ov/iso/zoneId/ground 按 **dword** 保留（游戏按 int32 读 ov，清错高半字 →
   0xFFFF 索引进 OverlayType 表崩溃 0x47D30F）；全清会波及渲染链
   （ra2md.dll+0x2BAA ddraw 崩溃）。只能清标量字段：
   0x100/0x10A/0x10C/0x10E/0x113-116/0x122/0x124/0x140/0xDD。

### RMG 变体之谜（矩阵验证结论）

**RMG 输出 = F(16 参数, 生成入口时的格子残留状态)**：

1. 主 RNG 每局入口由 `Randomizer::Seed(MapSeed.Seed)`（0x598985）重播种——
   主随机流完全确定。
2. 副随机流 [0xA8B230]+0x218 每局仅被 BuildingClass ctor 抽 2-4 次，**不影响地形**。
3. **真正的变体源：上一局地图的格子残留**。开机后格子表（0x87F924）全 NULL；
   首局生成后格子常驻。后续生成的放置/重试逻辑读旧格内容 → 重试次数变 →
   主流位置偏移 → 全图变体。
4. **CLI（零初始状态）== 开机首局**（seed1、seed12345 实测字节级）。
5. 阶段 7 科技建筑计数实测：`m = PickBelow(5)`（0x5A9645 用主流 0xABE890）。

### 会话内重置已判死

官方析构 `Scenario_ClearClasses`（0x6851F0，void cdecl 无参）两条调用路径都崩：
①RMG 入口 hook 内直调：c000041d 回调内致命异常；②WndProc 子类化 + WM_APP+0x42
在消息边界调：重置成功，但**下一局生成崩**——ClearClasses 摘掉 House 数组里
RTTI==0x12 的项（含 Neutral），阶段 7 FindHouseByType("Neutral") 拿不到 owner。
**结论：游戏就让它自然脏着，对齐走 CLI 脏链回放或 cleanregen 强制干净。**

## cleanregen 补丁（commit 4050749）

- 两枚补丁强制每局走原版完整重置路径：0x599A20 `jne 0x599B1D`→`jmp`、
  0x599BD3 `mov [esp+0xf],bl`→NOP（flag 恒 1）。
- 效果：c1==c2==c3==自然不动点 0 差异；新进程 gen#1+cleanregen == 自然干净首局。
  **8960 案穷举无需链语义，任意顺序采集即可**。
- rmgctl `cleanregen on/off`（OpenJob(true) 可 inject 前预置）。

## x64dbg headless 监督器（正确方法）

`.qwen/tmp/_headless_tiberium.py`（v4，计数驱动版）：

- spawn `E:\code\x64dbg\release\x32\headless.exe`，stdin 发命令：
  `attach <pid_hex>` → `bp <addr_hex>` ×N → 读回内存验证 0xCC
- 解析 stdout 的 `INT3 breakpoint at gamemd.XXXXXXXX!` **逐行计数**判定命中
  （不要按 EIP 轮询去重；也不能按 (tid,esp) 去重——同调用点 esp 相同）
- 命中后 OpenProcess 读写内存做注入/dump，然后 stdin 发 `run`
- 外来暂停（TLS 回调 bp、DLL 加载）：stdout 出现 `[STATE] paused` 但不在我的断点上，
  1 秒后自动 `run` 放行
- 参数注入：入口 0x598960 断下时写 MapSeedClass(0xABDFD8)+0x38..+0x74（16 字段）
- 监督器被杀会连带游戏退出（debuggee 随 debugger 死）
- 其他坑：x64dbg 断点库会跨会话恢复残留断点需 bc 清理；强杀调试器会在游戏内存
  留 0xCC 导致无调试器时执行到即崩，attach 后须先恢复原字节

### 失败方法（别再用）

- ctypes 自写调试器循环（DebugActiveProcess + WaitForDebugEvent）会卡死游戏
  （2026-10-05 曾验证只读附加可行，但完整断点循环不可靠；以 x64dbg headless 为准）。
- `_capture_live_stages_62298.py` 的 STAGES 表里 hills/lats/done 四个地址全部 +1 错位。

## 副随机流确定化（live_seed_hook/）

- 播种链：timeGetTime() → [0xA8ED94](Game::Seed) → Randomizer::Seed ×2 →
  rep movsd → [ScenarioClass]+0x218 与 0x886B88 两条流；进程启动时播种一次。
- 等价方案：rmg_done 断点处直写固定 R250 流状态（+4 起 1008 字节：
  i1=0/i2=103/state[250]，+0 虚表指针不动）。固定流 blob：
  `live_seed_hook/fixed_stream.bin`（seed=0x5EED0001）。
- 验证：两局 62298 无操作跑到 602 帧，cells_9b 与主 RNG 状态全部 0 差异——
  对局全程（矿生长+AI+一切副随机流消费）完全确定。

## 副产品想法（待办记录）

这套「headless 监督器 + 参数注入」可做成**地图种子固定化修改器**
（强制任意 Seed 玩同一张图）。日后立项。

## 关键地址

| 地址 | 说明 |
|------|------|
| 0x598960 | RMG 入口（注入点） |
| 0x5A23A0 | PlaceTiberium（tib 入口快照点） |
| 0x5A28C0 | CreateOreField（每片矿田入口；args: esp+4=seed*, +8=count, +C=fid, +10=gems） |
| 0x598EF2/0x598EF9 | PlaceTiberium 调用点/返回点（布矿前后快照点） |
| 0x599171 / 0x599215 / 0x599354 / 0x5994B0 | before_hills / before_lats / after_lats_recalc / rmg_done（阶段消息 push 处） |
| 0xABE890 / 0xABDFB8 / 0xABDFD8 / 0xABED10 | 主RNG / NormalDeviate / MapSeedClass / work数组 |
| 0x87F924 / 0x87F7E8 / 0xA8B230 | 格子指针表 / MapClass::Instance / ScenarioClass |

## 关键脚本（.qwen/tmp/）

| 脚本 | 用途 |
|------|------|
| `_clean_sweep.py <seed> [--start N]` | 游戏侧批量采集（cleanregen，崩溃自动重启，断点续跑） |
| `_cli_sweep.py <seed> <th>` | CLI 侧分块批量（每块 224 案，.done 断点续跑） |
| `_sweep_cmp.py <seed>` | 用已有 CLI 批量对拍全量 → `sweeps_cmp_<seed>.txt` |
| `_rerun_and_cmp.py <seed> [子串过滤]` | 单案重跑当前 CLI + 与游戏真值对拍（修复验证首选） |
| `_game_session.py` | ensure_game() / gen_clean() / gen_to() 自动化（不弹窗） |
| `_mt0fix_delta2.py` / `_p3_selfcheck.py` | 布矿入口态 emu 对拍 |
| `_mt1_p5_probe.py` / `_mt1_p6_probe.py` | mt1 两症状取证探针 |
| `_headless_tiberium.py` | headless 采集监督器（改 BPS/逻辑即可复用于其他阶段） |
| `_zero_ext_fields.py` | 活体按 offset 清格子字段+gen+dump |
| `_reconf_cli.bat` | 构建 CLI |

## 参考：E:\code\RA2YR_ReSource

他人全二进制重建项目，只有函数名/类布局有价值（RandomMap::Generate=0x598960、
SetupTheater=0x599650 等），RMG 无实现，我们远超。真实实现仅 472/19067 函数（2.5%），
exe 能链接不能跑（810 个 return-0 桩）。
