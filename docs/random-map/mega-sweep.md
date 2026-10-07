# 8960 案大穷举（mega sweep）——口径、数据位置与续跑

> 来源：原 AGENTS.md 2026-10-07 收工存档（kimi-k3）。本文是穷举任务的唯一权威口径。

## 一句话目标

让 CLI 重写版 RMG 与原版 gamemd.exe **逐字节对齐**：对种子 12345 和 62298，
各开一次游戏会话，连续生成**全部 8960 种参数组合**的地图并 dump 真值；
修完所有发现的 CLI bug，使每案 CLI 连生输出与游戏 rmg_done dump 12 字段全一致。

## 穷举口径

案数 = **4480/种子 × 2 种子 = 8960**。参数维度（核实自 `seds/order_12345.txt`）：

| 参数 | 取值 | 含义 |
|------|------|------|
| mt (MapType) | 0,1,2,3,4（5 个） | 地图类型/地形风格 |
| t (Time) | 0,1,2,3（4 个） | 时间（**实测对 cells 零影响**，只改光照） |
| th (Theater) | 0=温带, 1=雪地 | 气候/剧院 |
| sz (RegionSize) | 0,1,2,3（4 个） | 尺寸 |
| r (Resources) | 0,1,2,3（4 个） | 资源 |
| p (Players) | 2..8（7 个） | 玩家数 |

5×4×2×4×4×7 = 4480。（旧文本里"3584/7168"是 mt 扩维前的口径，已过时。）

## case 命名与数据位置

- 案名 = `s<seed>_mt<M>_t<T>_th<TH>_sz<SZ>_r<R>_p<P>`，例：`s12345_mt0_t0_th0_sz0_r0_p2`
- 每案一个 `.sed`：`mega_sweep/seds/<case>.sed`
- 顺序基准 = `mega_sweep/seds/order_<seed>.txt`（每行一个 case，**对拍映射靠它，别重排**）
- **游戏真值** = `mega_sweep/sweeps/<seed>/<case>.bin`（12B/格，512×512 = 3145728 B）
- **CLI 批量** = `mega_sweep/sweeps_cli/<seed>/th<T>_c<CC>_<II>.bin`
  （CC=块号=ORDER位置/224，II=块内序号+1，映射见 `.qwen/tmp/_sweep_cmp.py:cli_path()`）
- 根目录 = `E:\code\ra2-reverse-AI\snapshot_gamemd_62298\mega_sweep\`

### 12B/格布局（cli_main.cpp）

```
[0]exists [1..2]isoTile int16LE [3]subtile [4]level [5]ramp [6]od
[7..8]overlay int16LE(-1=无) [9]object占用布尔 [10]ground低字节 [11]zoneId
```

## 任务状态（2026-10-07 收工时冻结；10-07 晚已由 qwen-code 续跑）

| 任务 | 冻结状态 | 续跑命令 |
|------|----------|----------|
| 游戏侧采集 12345 | 2758/4480（10-07 晚续跑中） | `python .qwen/tmp/_clean_sweep.py 12345`（断点续跑+崩溃重启） |
| CLI 侧批量 12345 | ✅ 4480 案全完成 | — |
| CLI 侧批量 62298 温带 | 1344/2240（chunk6 曾因 CLI exe 被并发重建崩；10-07 晚续跑中） | `python .qwen/tmp/_cli_sweep.py 62298 0`（.done 断点续跑） |
| CLI 侧批量 62298 雪地 | 未开始 | `python .qwen/tmp/_cli_sweep.py 62298 1` |
| 游戏侧采集 62298 | 未开始 | 12345 跑完后 `python .qwen/tmp/_clean_sweep.py 62298` |
| mt0 修复（主战场） | 建筑占地已修（commit 77c7038），布矿发散待破 | 见 mt0-mt1-open-issues.md |
| mt1 修复（20 案单格） | 取证已固化，根因待挖 | 见 mt0-mt1-open-issues.md |

对拍基线（`_sweep_cmp.py 12345`，用修复前的 CLI 批量）：
`pass=1100 diff=1299 missing=2081`。diff 几乎全是 mt0/th0/sz0；**雪地 th1 全过**；
时间参数 t 对 cells 无影响。

## 游戏与修改器状态（采集前提）

- 游戏 gamemd 由用户保持运行（修改器已挂载 cleanregen 补丁）。
  另有一个无害僵尸 gamemd（杀不死，`rmgctl` 的 FindGame 会跳过它）。
- 干净变体采集模式（穷举的基础）：
  - 补丁两枚：`0x599A20` jne→jmp（强制走 0x599650 重置路径）、`0x599BD3` NOP（flag 恒 1）
  - `gen_clean()` = 点击生成前**外部清 `[MapSeed+0x178]`**（上一局参数快照指针，
    干净/不动点变体的唯一开关）→ 每局 == 进程首局干净变体 == CLI 单局（已逐字节验证）
  - ⚠️ 在 RMG 入口（hook 内 InjectParams）清 +0x178 会产生**第三变体**
    （格子表延迟分配只有 3539 格）——已 revert，`code/modifier/dllmain.cpp` 有注释
- 游戏导航/注入/生成全自动化（不弹窗）：
  `.qwen/tmp/_game_session.py` → `ensure_game()` / `gen_clean(path,pid,sed)` / `gen_to()`
- **游戏一次只能被一个采集流程占用**，批量采集与活体断点采集互撞（别同时跑）。

## 构建

- CLI：`cmd //c .qwen/tmp/_reconf_cli.bat`
- 修改器：`cmd //c code/modifier/build_cmake.bat`（dll 被锁时先 os.rename 旧 dll 再构建）

## 踩过的坑（本轮新增）

1. **不 push**（commit 已获用户允许，只本地 commit）
2. **绝不弹 MessageBox**（发现弹窗一律当"闹鬼"：沿 ParentProcessId 链查 node PID，
   查该 node 的 `agents/*/wire.jsonl` 是否还在增长）
3. **git commit 只精确 add 源码路径，绝不 `git add -A`**（曾误加数千探针/build 文件）
4. 子代理超时不等于 loop 终止（同进程协程仍在跑）——必须用 `agents/<id>/wire.jsonl`
   的 mtime + `agent.turn.ended:aborted` 记录验证，并清理其派生的孤儿 python
   （`Get-CimInstance Win32_Process` 查命令行后 Stop-Process）
5. 动态 debug 用 x64dbg headless（ctypes 自写调试器会卡死游戏，已证伪）；
   BP 计数靠解析 stdout `INT3 breakpoint at gamemd.XXXXXXXX!` **逐行计数**，
   不要按 (tid,esp) 或 EIP 轮询去重
6. **BP 地址必须先用 `python code/randmap/disasm.py 0x<addr> 0x20` 验证是指令边界**
   （历史踩过 +1 错位：0xCC 落指令中间会破坏操作数且不触发）
7. dll 被锁（僵尸 gamemd 持有旧 dll）→ **改名规避**（Windows 允许对有活跃映射的
   文件 rename）——`os.rename` 后即可重新链接
