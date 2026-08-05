# YR 游戏运行流总览（Game Loop）

> 逆向目标：原版 `gamemd.exe`（2001-10-31，MSVC 6.0，x86，ImageBase `0x400000`）
> 回答的问题：**程序启动后先干什么后干什么？玩家开一局、打完输赢、退出，整个流程长什么样？**
> 这是给后续机制逆向绘制的"地图"——每个机制（弹道、采矿、寻路、AI）都挂在本文的某个节点上。

## 一句话结论

YR 是一个 **"初始化 → 会话选择循环 → 单局循环"** 的三层结构，与 RA1 源码
（`CONQUER.CPP`）**同源演化**：单局循环每帧跑 `MainLoop`（逻辑更新）+
`AuxLoop`（结束事件分派），胜负由 **HouseClass 内部标志**（0x1F7 胜利 / 0x1F8 被消灭）
在每帧判定中写入 4 个**全局结束事件标志**，`AuxLoop` 按标志调用
`DoWin / DoLose / DoRestart / DoExit` 完成结算并**重开场景**或返回选关。

## 运行流全景

```
WinMain @ 0x6BB9A0                        ── 程序入口
├─ Launcher 检查 → 单实例 Mutex → AutoPlay 互斥
├─ 命令行解析 + 读 RA2MD.INI（Video/Network）
├─ OleInitialize + 批量 CoRegisterClassObject（COM 类注册）
├─ DirectDraw 初始化（800x600 / 640x480 降级）→ 等焦点 → 通知 launcher
├─ Direct3D 检查 + ZBuffer/ABuffer + 主界面对象 DAT_00887640
├─ 日志 "Main Game" → FUN_0048CCC0()  ★MainGame★
└─ 返回后 PostMessage(WM_QUIT) → 清理 COM/Mutex/DirectDraw → OleUninitialize

MainGame @ 0x48CCC0                      ── 游戏主流程（= RA1 Main_Game）
└─ while (FUN_0052D9A0())                ── 会话选择循环（= RA1 Select_Game）
   │    （选关/读档/联机/退出，返回 1=进入游戏）
   ├─ 场景准备（重置标志、Session::Resume）
   ├─ do {
   │    ├─ MainLoop() @ 0x55D360         单帧游戏循环
   │    ├─ FUN_0048C8B0()                SpecialDialog 对话框处理
   │    └─ AuxLoop() @ 0x55CFD0          结束事件分派（DoWin/DoLose/...）
   │   } while (!AuxLoop 结束)
   └─ 局结束清理（按 Session 类型分支）

开局链：
FUN_0052D9A0 → ScenarioClass::Start @ 0x683AB0
  → 场景加载层 FUN_00684620 @ 0x684620
     ├─ 随机场景判定（"Scen > IsRandom"）
     ├─ FUN_00686730 @ 0x686730（固定场景）→ ReadScenario_MissionINI @ 0x686B20
     │      （清旧场景 ClearClasses → 读 INI → 建 House/地图 → 进度条 → 场景激活）
     └─ FUN_00597A10（随机地图：遭遇战/联机）
  → MovieInfo::ScenarioStarted=1, TacticalActive=1 → 进入 MainLoop
```

## 1. 程序启动（WinMain @ 0x6BB9A0 ~ 0x6BE1BF）

| 步骤 | 证据 | 说明 |
|---|---|---|
| 版本/类型大小日志 | `FUN_004068e0("_%d_%d", 0x67c~0x6b3, sizeof)` | 启动即打印各类型大小（对齐检查） |
| Launcher 检查 | `FUN_0049f5c0()` → "Launcher not running, Bail" | **必须经启动器运行** |
| 单实例锁 | `CreateMutexA("29e3bb2a-2f36-11d3-...")` → "TibSun is already running" | 已运行时激活旧窗口并退出 |
| AutoPlay 互斥 | "Waiting for Autoplay to quit" | 等自动播放退出 |
| 命令行/工作目录 | `FUN_007c9cc2` 分割参数 + `SetCurrentDirectoryA` | |
| INI 读取 | RA2MD.INI `[Video] VideoBackBuffer/ScreenWidth/ScreenHeight`、`[Network] Socket/DestNet` | |
| COM 初始化 | `OleInitialize` + 数十次 `CoRegisterClassObject`（IID 0x7E9520~0x7E99xx） | 含存档序列化器 CLSID 0x7E9540 |
| 显示初始化 | `FUN_00777c30` + `FUN_004a42f0`（DirectDraw 表面，失败降 640x480） | |
| 等焦点 | `do { FUN_005d4d50(); } while (Game::IsFocused==0)` | |
| 通知 launcher | `FUN_0049f620()` → "Failed to notify launcher" | |
| 主界面对象 | `DAT_00887640 = FUN_007b8730(设备, hWnd)` | 全游戏最常用的接口对象（+0xC 渲染帧等） |
| **进入主循环** | 日志 "Main Game" → `FUN_0048CCC0()` | |
| 退出 | `PostMessageA(hWnd, WM_QUIT)` + `do{...}while(DAT_00a8edd8==1)`；然后 `CoRevokeClassObject` 全部、`RevokeActiveObject`、关 Mutex、DirectDraw 释放、`OleUninitialize` | |

## 2. 会话选择 / 选关（FUN_0052D9A0 @ 0x52D9A0 ~ 0x52EB57）

= RA1 `Select_Game()`。播放 INTRO 主题后进入 `do { ... }` 循环，每次开局前重置
全局（`Game::IsActive=1`、`CurrentFrame=0`、4 个结束标志清零、`RequestedFPS=30`），
然后按菜单选项 `switch(iVar11)` 分支：

| case | 行为 | 证据 |
|---|---|---|
| 1 / 4 | 单人战役选择（`FUN_0060D380`，`Game::ObserverMode=0`） | |
| 2 | 联机（`SessionClass::Instance=4` + `FUN_0053F1F0`） | |
| 3 | IPX 局域网（`Instance=3`） | |
| 5 | 遭遇战（`FUN_0055FC80`，`Instance=5`） | |
| 6 | 退出确认框（`FUN_005D3490`） | |
| 7 | **退出游戏**：`ThemeClass::Stop` + 返回 0 | 唯一的退出出口 |
| 8 | 过场动画（`FUN_00622650` 播放窗口 + AudioStream 事件循环） | |
| 9 | 读档（`LoadOptionsClass::LoadDialog`）→ 直接进游戏 | |
| 0xB/0x10/0x11 | 会话初始化 `FUN_006980C0`，按 Session 类型分 7 路（战役0/串口1/Modem2/IPX3/联机4/遭遇战5） | |
| -1 | 回放（读 `Game::RecordFile` 头部） | |

**进入游戏的统一出口**（LAB_0052E982）：
日志 "About to load a %d player game" → 联机时 `SessionClass::CreateConnections()`
+ `IPXManagerClass::SetTiming` → **`FUN_00683AB0(场景号)`**（战役传 `Scenario+0x34CC`
记录的场景号，联机/遭遇战传 -1）→ 录制文件写头部（`RecordingFlag & 1`）。

## 3. 开局链（ScenarioClass::Start 系列）

```
ScenarioClass::Start @ 0x683AB0
├─ 记录场景号 (Scenario+0x34CC)、日志 "Starting scenario %s" / "Player Count %d"
├─ 场景名拷贝到 Scenario+0x125C
├─ CD 校验 CCFileClass::CDCheck + 读 scenario INI（variables::ReadCCFile）
├─ 读 [Basic] Intro/Brief → ThemeClass::Play(LOADING)
└─ FUN_00684620 @ 0x684620  ← 场景加载层
   ├─ CurrentFrame=0；随机场景判定（"Scen > IsRandom: true/false"）
   ├─ 联机：UDP 广播地址设置（"Adding broadcast address"）
   ├─ 加载进度条 LoadProgressManager + 阵营侧边条（SPLDBR.SHP / PROGBARM.SHP）
   ├─ 固定场景：FUN_00686730 @ 0x686730
   │    └─ ReadScenario_MissionINI @ 0x686B20  ← 场景初始化主函数
   │         ├─ 日志 "Clearing old scenario" → Scenario_ClearClasses @ 0x6851F0
   │         ├─ 读地图 INI（MISSIONMD.INI 战役 / 联机读 Countries/General/Waypoints）
   │         ├─ RadarClass::CreateEmptyMap / ReadStartPoints（联机）
   │         ├─ 进度条 LoadProgressManager::Draw + FUN_0069AE90(N) 步进
   │         ├─ SwizzleManagerClass::Instance 初始化（FUN_006CF230）
   │         ├─ RulesClass::Read_File + 类型注册 + LoadArt
   │         └─ MapClass::sub_657CE0 场景激活（存档系统同款）
   ├─ 随机地图（遭遇战）：FUN_00597A10
   ├─ 失败：日志 "Error: Unable to read scenario" + WWMessageBox
   └─ 成功：MessageListClass::Init → 等所有玩家就绪（进度<100% 循环）
MovieInfo::ScenarioStarted=1, TacticalActive=1（ScenarioClass::Start 结尾）
```

## 4. 单帧循环（MainLoop @ 0x55D360 ~ 0x55DEDB）

Phobos hook 点：Begin 0x55D360 / SaveGame 0x55DBCD（"right before **LogicClass::Update()**"）
/ NewMessageListManage 0x55DDA0 / End 0x55DED5。

```
if (Game::IsActive==0) 直接返回
DAT_00ABCD58 = 1                                  ← "游戏中"标志
失焦：if (!Game::IsFocused) Sleep(10/500) + FUN_005D4D50 循环
帧步进检查（FrameStep 模式，DAT_00A8E2E4 vs CurrentFrame）
帧时间戳 _DAT_00A8B55C = timeGetTime()
音乐管理（ThemeClass，TacticalActive 时）
帧定时：单机用 GameOptions 速度；联机按 Game::RequestedFPS（0x3C/FPS）
输入采集 + 渲染：FUN_004F4320 + FUN_0055DEE0 + GScreenClass::Render（无对话框且有焦点时）
录制/回放：RecordingFlag &1 写帧数据 / &2 读帧数据 + 目标列表校验（CCFileClass）
LayerClass::Sort()                                ← 地面层排序（= RA1 Layer[GROUND].Sort）
单机首帧自动存档（ScenarioClass::WasGameSaved，写 MISSION 存档）
LogicClass::Update()                              ★核心逻辑更新（单位/建筑/AI/胜负判定触发源）
键盘快捷键（InputManager::IsKeyPressed ×4 → 发事件 FUN_004A9840）
MessageListClass::Manage()                        ← hook 0x55DDA0（= RA1 Messages.Manage）
帧时间统计 DAT_00A8B560/564
FUN_0048D080()                                    ← CallBack：Windows 消息泵 + 挂起回调 + 网络包
检查 4 个结束事件标志 → 无则 CurrentFrame++，FUN_00725C70()，返回 Game::IsActive==0
```

## 5. 对话框与结束事件分派

### SpecialDialog 处理器（FUN_0048C8B0 @ 0x48C8B0）
MainGame 每帧在 MainLoop 后调用，= RA1 `if (SpecialDialog != SDLG_NONE) switch(...)`：

| case | 行为 |
|---|---|
| 1 | 简报（FUN_004F10E0） |
| 2 | **投降**：FUN_005C60D0 确认框 → 发 EventClass 事件 + FUN_006471A0 |
| 3 | 多人菜单（FUN_004F1840；**case 5 里 `DAT_008B41C0 = 1`** = 投降标志触发点） |
| 4/6 | 切换对话框（→5） |
| 5 | 选项（FUN_004E1D00） |
| 7/8/9 | 其他对话框 |

结束后：`Game::SpecialDialog=0` → `UpdateWindow` → `SessionClass::Resume()`（恢复速度）→ `FUN_00683FB0`。

### AuxLoop（FUN_0055CFD0 @ 0x55CFD0 ~ 0x55D359）——结束事件分派
MainGame 里 MainLoop 结束后调用，检查 4 个**结束事件标志**，返回 `Game::IsActive==0`：

| 标志 | 触发方 | 处理函数 | 含义 |
|---|---|---|---|
| `0xA83D49` | HouseClass 胜负判定 | **DoWin @ 0x685670** | 胜利 |
| `0xA8ECD0` | HouseClass 胜负判定 | **DoLose @ 0x685DC0** | 失败 |
| `0x8B41C0` | SpecialDialog case3（投降/退出多人菜单） | **DoRestart @ 0x6863E0** | 重开本关 |
| `0xA83D48` | FUN_004C6CB0（事件） | **DoExit @ 0x686570** | 退出本局回菜单 |

联机额外处理：断线 "Disconnect Gracefully"（DAT_00A83D48 分支内）。

## 6. 胜负判定（HouseClass 成员 @ 0x4F8440 区）

> 该区域 Ghidra 未建函数，汇编逐条还原（`memory/data/decomp/gameloop_victory_asm.txt`）。
> `this = HouseClass*`，每帧对每个 House 检查其内部标志，判定后写入全局结束标志。

```
if (house->+0x1F7) {                    // "达成胜利条件"（任务目标/联机胜利条件）
    播放结束动画（FUN_007529E0 + 300ms 超时循环 + FUN_0048D080）
    清 house->1F7
    若 玩家house指针==NULL：             // 玩家 house 已释放 = 玩家没了
        house->1EC/1ED 任一为真 → 胜利标志 0xA83D49=1
        全假                          → 失败标志 0xA8ECD0=1（兜底）
    否则若 this == 玩家house(0xA83D4C)：→ 胜利标志 0xA83D49=1（自己达成 = 赢）
    联机(3/4)：连接正常且 house 非玩家  → 胜利标志 0xA83D49=1
    兜底                                → 失败标志 0xA8ECD0=1
}
if (house->+0x1F8) {                    // "被消灭"
    播放结束动画（同上）
    清 house->1F8
    this == 玩家house(0xA83D4C)         → 失败标志 0xA8ECD0=1（自己被灭 = 输）
    兜底（敌人被灭）                     → 胜利标志 0xA83D49=1
}
if (house->+0x1F6) {                    // 收尾：清 1F6 + CALL 0x4FC6D0（清场）
    ...
}
```

**触发源头**：
- 被消灭（1F8）：`HouseClass::FlagToDie` 等调用设置（联机由事件 case 0x23
  REMOVEPLAYER → `HouseClass::FlagToDie()`，FUN_004C6CB0 @ 0x4C6CB0 事件执行器）
- 达成条件（1F7）：战役任务目标（触发器等）在 `LogicClass::Update` 逻辑链内设置
  （具体设置点未逐一确认，见"未确认"）

## 7. 结算与重开（DoWin / DoLose / DoRestart / DoExit）

四个函数结构高度对称（`memory/data/decomp/gameloop_main_decomp.txt`）：

| 步骤 | DoWin 0x685670 | DoLose 0x685DC0 |
|---|---|---|
| 联机统计 | `FUN_00648350` + `FUN_0055CF10` | 同左 |
| 停场景 | `MovieInfo::ScenarioStarted=0, TacticalActive=0` | 同左 |
| 计时结算 | 累计 `Scenario+0x61C` | 同左 |
| 结算画面 | `FUN_007529E0` 循环 + 300ms 超时 | 同左 |
| 显示恢复 | "Toggle display mode back to shell" + `FUN_00560BF0` | 同左 |
| 战役分支 | 下一关检查（Scenario+0x1448/0x144C）→ `Game::IsActive=0` | **重试对话框** `FUN_005D3490`（Phobos hook 名证实） |
| 联机分支 | `FUN_005DB680`（返回房间统计）失败则退出；重建 WOL 连接 `FUN_007B0D90` | 同左 |
| **重开场景** | `FUN_00683AB0(Scenario+0x34CC)`（下一关/重试同一关） | 同左 |
| 联机重建 | `SessionClass::CreateConnections` + `IPXManagerClass::SetTiming` | 同左 |

- **DoRestart @ 0x6863E0**：清场 → 停主题 → **60 秒倒计时**（0x3C）→ `FUN_00683AB0(场景号)` 重开
- **DoExit @ 0x686570**：停场景 → `Game::IsActive=0`（回选关循环），联机含断线清理

## 8. 退出流程

选关 case 7（或 DoExit 置 `Game::IsActive=0` 后 MainGame 外层循环条件失败）
→ MainGame 返回 → WinMain：`PostMessage(WM_QUIT)` → 等窗口处理 →
`CoRevokeClassObject` 全部 COM 类 → `RevokeActiveObject` → 关 AutoPlay/App Mutex →
DirectDraw 释放（`FUN_0053E1D0`）→ `OleUninitialize` → 返回。

## RA1 ↔ YR 对照表

| RA1（CnCRemastered 源码） | YR（gamemd.exe） | 对应程度 |
|---|---|---|
| `DLL_Startup`（STARTUP.CPP） | WinMain @ 0x6BB9A0 | 结构同源，YR 多 COM 类注册 |
| `Main_Game`（CONQUER.CPP:212） | MainGame @ 0x48CCC0 | ✅ 完全对应 |
| `Select_Game`（INIT.CPP:459） | FUN_0052D9A0 @ 0x52D9A0 | ✅ 完全对应（switch 选项一致） |
| `Main_Loop`（CONQUER.CPP:2150） | MainLoop @ 0x55D360 | ✅ 逐步对应（定时/输入/逻辑/消息/胜负） |
| `SpecialDialog` switch（CONQUER.CPP） | FUN_0048C8B0 + AuxLoop 0x55CFD0 | ✅ 对应（YR 用全局标志替代） |
| `PlayerWins/PlayerLoses/PlayerRestarts` | `0xA83D49/0xA8ECD0/0x8B41C0(+0xA83D48)` | ✅ 对应（YR 多一个退出标志） |
| `Do_Win/Do_Lose/Do_Restart` | DoWin 0x685670 / DoLose 0x685DC0 / DoRestart 0x6863E0 | ✅ 对应 |
| `Logic.AI()` | `LogicClass::Update()`（MainLoop 内） | ✅ 对应 |
| `Call_Back()` | `FUN_0048D080` | ✅ 对应 |

## 关键全局

| 地址 | 语义 | 依据 |
|---|---|---|
| `0xA8B230` | ScenarioClass::Instance（场景对象） | WinMain/各处 |
| `0xA83D4C` | 玩家 HouseClass 指针 | 胜负判定 this==玩家 比较 |
| `0xA83D49` | 结束事件 1：胜利 → DoWin | AuxLoop 分支 |
| `0xA8ECD0` | 结束事件 2：失败 → DoLose | 同上 |
| `0x8B41C0` | 结束事件 3：投降/重开 → DoRestart | SpecialDialog case3 写入 |
| `0xA83D48` | 结束事件 4：退出/断线 → DoExit | FUN_004C6CB0 写入 |
| `0xABCD58` | "游戏中"标志（MainLoop 置 1/0） | MainLoop |
| `0xA8ED84` | 全局帧时间戳（结束延迟计时基准） | 胜负判定汇编 |
| `0xA8B238` | Session 类型（==3 IPX / ==4 联机） | 胜负判定汇编 CMP 3/4 |
| `0x8B23C` → 实为 `0xA8B23C` | 联机会话对象（WOL/IPX 接口，vtable 调用） | MainLoop/AuxLoop |

## 未确认 / 待验证

1. **胜负判定函数的调用者**：0x4F8440 区 HouseClass 成员由谁每帧驱动（推测
   `LogicClass::Update` 遍历 House 数组时调用，未追调用链）——反编译
   `LogicClass::Update` 可确认
2. **house+0x1F7 的写入点**：战役"达成胜利条件"标志由谁设置（触发器等，未确认）
3. **house+0x1EC/0x1ED 的精确语义**：胜负判定中"存活/投降"判断的依据（未确认）
4. **0xA8B238 vs SessionClass::Instance 的关系**：反编译中同时出现两者，地址关系未核对
5. **WinMain 中 COM 类清单**：0x7E9520~0x7E99xx 数十个 IID 对应的具体 COM 对象
   （序列化器 0x7E9540 已确认，其余未逐一定位）
6. **过场动画链**：case 8 的 `FUN_00622650` 播放器细节（未展开）
7. **LogicClass::Update 内部**：作为"全游戏逻辑心脏"，内部结构尚未解剖
   （后续机制逆向的挂载点）

## 9. 玩家旅程（日常一局：开机 → 遭遇战 → 打赢 → 退出）

> 把上面的运行流映射到一次普通玩家的操作路径。**已实锤**的带地址，
> **推断**的标注"未确认"（`LogicClass::Update` 内部未解剖）。

### 全景：5 分钟 = 4 个阶段

```
你启动 gamemd.exe
  → ① WinMain @ 0x6BB9A0   程序初始化
你点遭遇战、选国家、点开始
  → ② 选关循环 @ 0x52D9A0 + 开局链 @ 0x683AB0
你指挥部队（每秒 ~30 帧）
  → ③ MainLoop @ 0x55D360  单帧循环
你打赢 → 胜利画面 → 点退出
  → ④ 胜负判定 @ 0x4F8440 → DoWin @ 0x685670 → 回主菜单 → 退出
```

### ① 开机（WinMain @ 0x6BB9A0）
launcher 拉起 `gamemd.exe` → Launcher 检查（`FUN_0049F5C0`，非 launcher 直接退出）
→ 单实例 Mutex（`CreateMutexA("29e3bb2a-...")`）→ 等 AutoPlay 退出 → 命令行/RA2MD.INI
（`[Video]` 分辨率）→ `OleInitialize` + 批量 `CoRegisterClassObject`（COM 类注册）→
**DirectDraw 初始化**（800x600，显存不足降 640x480）→ **等窗口焦点**
（`do{FUN_005D4D50();}while(Game::IsFocused==0)`）→ 通知 launcher →
日志 **"Main Game"** → `MainGame @ 0x48CCC0`。

### ② 主菜单 → 遭遇战（选关循环 @ 0x52D9A0）
`MainGame` 核心 = `while (FUN_0052D9A0()) { 打一局 }`——**每局都是一次循环**。
`FUN_0052D9A0`：播放 INTRO 主题 → 重置全局（`Game::IsActive=1`、`CurrentFrame=0`、
4 个结束标志清零、`RequestedFPS=30`）→ `switch(菜单选项)`：
- **点"遭遇战"** → `case 5`：`FUN_0055FC80` → `SessionClass::Instance = 5`
- **选国家** → `Game_ComScenarioDialog`（Phobos hook `Game_ComScenarioDialog_ChatBox` 一串
  @ 0x55E477 区）；国家最终落每个 `HouseClass::Side`（`DAT_00A8EB64`）。**界面交互细节未确认**
- **点开始** → `case 0xB/0x10/0x11` 会话初始化 `FUN_006980C0`（遭遇战走 `FUN_006AE2C0`，
  未展开）→ 日志 **"About to load a 2 player game"** → **`FUN_00683AB0(-1)`**（遭遇战传 -1）

### ③ 开局加载（ScenarioClass::Start @ 0x683AB0 → 加载层 @ 0x684620 → ReadScenario @ 0x686B20）
| 你看到的 | 程序实际 | 证据 |
|---|---|---|
| "Starting scenario..." | 场景号记录 + `"Starting scenario %s"` 日志 | 0x683AB0 开头 |
| 进度条 | **随机场景判定**（"Scen > IsRandom: true"）→ 遭遇战走**随机地图** `FUN_00597A10` | 0x684620 |
| 进度条 | 清旧场景（"Clearing old scenario" → `Scenario_ClearClasses` @ 0x6851F0） | 0x686B20 |
| 进度条 | 读地图 INI（`[Basic] Player` 阵营、`RulesClass::Read_Countries`、`LoadArt`） | 0x686B20 |
| 进度条 | **建 House**（`DAT_00A80238` House 数组；玩家指针存 `0xA83D4C`） | 0x686B20 |
| 进度条 | SwizzleManager 初始化（0x6CF230） | 0x686B20 |
| 进度条走完 | `MapClass::sub_657CE0` 场景激活 → `ScenarioStarted=1, TacticalActive=1` | 0x683AB0 结尾 |

> 遭遇战进度条用 `PROGBARM.SHP`（战役用 `SPLDBR.SHP`）。

### ④ 单局循环（MainLoop @ 0x55D360，每帧 30FPS）
```
if (Game::IsActive==0) 返回
失焦 → Sleep 空转等焦点
帧定时（RequestedFPS → 帧间隔 ~33ms）
输入采集 + 渲染（FUN_004F4320 + GScreenClass::Render）← 你看到的画面
LayerClass::Sort()
LogicClass::Update()    ★逻辑心脏：单位/建筑/子弹/AI/伤害/胜负检查（内部未解剖）
键盘快捷键 → 事件
MessageListClass::Manage()
FUN_0048D080()（Windows 消息泵 + 网络包）
CurrentFrame++
```
`LogicClass::Update` 是"全游戏逻辑"唯一入口（Phobos hook `MainLoop_SaveGame @ 0x55DBCD`
注释原文 "right before LogicClass::Update()"）。另外**单机首帧自动存档**。

### ⑤ 你赢了（胜负判定 @ 0x4F8440 区，汇编逐条还原）
你消灭最后一个敌人 → 敌人 House 被标记：
```
每帧对每个 House 检查：
if (house->+0x1F8 被消灭) {
    结束动画（300ms 超时）
    this == 玩家house(0xA83D4C)？→ 失败标志 0xA8ECD0 = 1
    否则（敌人被灭）          → 胜利标志 0xA83D49 = 1   ★你走这里★
}
if (house->+0x1F7 达成条件) { 战役才有
    this == 玩家？→ 胜利；敌人 → 失败
}
（写入点：0x4F867C / 0x4F8692 / 0x4F86EE / 0x4F87BB）
```
触发源：联机被移除 = 事件 `case 0x23 REMOVEPLAYER` → `HouseClass::FlagToDie()`
（事件执行器 `FUN_004C6CB0 @ 0x4C6CB0`）；遭遇战 AI 被消灭的触发调用点**未确认**；
house+0x1F7 写入点**未确认**。

### ⑥ 胜利画面（DoWin @ 0x685670）
帧末 MainLoop 检测到结束标志 → `AuxLoop @ 0x55CFD0` 分派 → `DoWin`：
```
MovieInfo::ScenarioStarted=0, TacticalActive=0    停战术画面
结算游戏时间（Scenario+0x61C）
FUN_007529E0() 循环显示结算画面 + 300ms 超时      ← "你赢了"
恢复显示模式（"Toggle display mode back to shell"）
遭遇战分支（非战役非联机）：DAT_00A8D57C++（胜利计数）+
    FUN_005C9700/FUN_005C9720（统计清理，未确认）→ Game::IsActive = 0
```
`Game::IsActive=0` = 整局"关灯开关"（MainLoop 开头检查、AuxLoop 返回它）。
输了走 `DoLose @ 0x685DC0`（对称，战役有重试框）；投降走 `DoRestart @ 0x6863E0`
（60 秒倒计时重开）；断线走 `DoExit @ 0x686570`。

### ⑦ 退出
`Game::IsActive=0` → MainGame 跳出单局 → `while (FUN_0052D9A0())` 回主菜单 →
点"退出" → `case 7`：`ThemeClass::Stop` + return 0（唯一出口）→ MainGame 返回 →
WinMain：`PostMessage(WM_QUIT)` → 撤销 COM 类 → 关 Mutex → DirectDraw 释放 → `OleUninitialize` → 进程退出。

### 旅程中的未确认点（接续逆向的入口）
1. `LogicClass::Update` 内部遍历顺序（弹道/采矿/AI 都在这）
2. 遭遇战选国家界面交互细节（`Game_ComScenarioDialog` @ 0x55E477 区）
3. 遭遇战 AI 玩家的"思考"调用点
4. house+0x1F8 触发调用点（联机入口已确认，本地/AI 未确认）
5. 胜负判定函数（0x4F8440）的每帧调用者

---

## 取证文件

| 文件 | 内容 |
|---|---|
| `code/ghidra_scripts/decompile_gameloop.py` | 第一轮：11 个生命周期锚点函数反编译 |
| `code/ghidra_scripts/decompile_gameloop2.py` | 第二轮：WinMain（300s 超时）+ 结束事件函数 |
| `code/ghidra_scripts/decompile_gameloop3.py` | 第三轮：场景加载层 + 结束标志引用点 |
| `code/ghidra_scripts/decompile_gameloop4/5.py` | 第四/五轮：胜负判定区汇编 + 事件执行器 |
| `memory/data/decomp/gameloop_main_decomp.txt` | 第一轮反编译全文（MainGame/MainLoop/AuxLoop/DoWin/DoLose/ReadScenario） |
| `memory/data/decomp/gameloop_winmain_decomp.txt` | WinMain 全文 + 结束事件函数 |
| `memory/data/decomp/gameloop_scenario_decomp.txt` | 场景加载层 + 标志引用点清单 |
| `memory/data/decomp/gameloop_victory_asm.txt` | 胜负判定汇编（0x4F8440 区）+ 事件执行器 FUN_004C6CB0 |
