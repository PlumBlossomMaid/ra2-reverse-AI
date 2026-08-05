# 崩溃排查：超时空移除可驻军建筑

**报告状态**：初版（崩溃点三选一，待运行时地址精确命中）

## 场景

- 步兵执行 **Enter 任务**（正在走向/进入房子，人还在外面，未进入 Occupants 列表）
- 超时空军团兵（Chrono Legionnaire）对房子施加 **Temporal 冻结**（房子被 warp out，直接消失，不走伤害流程）
- 房子消失 → 游戏弹窗崩溃
- 战斗碉堡（Battle Bunker）同样可触发，确认是原版机制问题

## 时间线（崩溃前发生了什么）

```
超时空军团兵开火
  → TemporalClass::Fire @ 0x71AF20   挂接目标, WarpPerStep = Type->+0xA0 × 10
  → 建筑 DisableTemporal @ 0x4521C0  建筑被冻结禁用
  → [Target->vtable+0x124](2)        目标挂进 Temporal 链

每帧
  → TemporalClass::Update @ 0x71A760  WarpRemaining -= WarpPerStep
  → 0x71A88D  WarpRemaining ≤ 0 → 冻结完成
  → 0x71A8BD  播放 warp-away 动画
  → 0x71A9A7  [建筑] KillOccupants(0) 杀驻军 (GetOccupantCount>0 时)
  → 0x71AA4F  [Target->vtable+0xf8]() ← 移除建筑 (Detach 广播)
  → 0x71AAD5  [Owner->vtable+0x484](0,1) 通知超时空单位
  → 0x71AAE7  清 this->Target 等字段 (在移除之后!)
```

**关键点**：`this->Target` 在目标被移除（0x71AA4F）之后才清零（0x71AAE7），
中间窗口内 TemporalClass 仍持有指向已移除建筑的指针。

## 原版崩溃点（Phobos 逐一定位修复）

### A. 0x51BB7A —— 步兵 AI 悬垂虚调用（最高嫌疑）

`InfantryClass::AI` 内（Phobos hook `TechnoClass_AI_TemporalTargetingMe_Fix`，
Building/Aircraft/Unit/Infantry 四类通用）：

```asm
0x51BB6E  MOV ECX,[ESI+0x278]   ; pThis->TemporalTargetingMe (瞄准我的 TemporalClass)
0x51BB74  TEST ECX,ECX
0x51BB76  JZ  0x51BB7D
0x51BB7A  CALL [EAX+0x5c]       ; → TemporalClass::Update()
0x51BB7D  ...                   ; ← Phobos 崩溃栈: "stack dump starts with 0051BB7D"
```

原版**不检查 TemporalClass 是否还活着**。目标 warp out 时该 TemporalClass 被销毁，
步兵 `TemporalTargetingMe` 指针未清 → 悬垂虚调用。
Phobos 修复：vftable 校验；已销毁则步兵 `Limbo()` + `UnInit()`（步兵也消失）。

### B. 0x71ADE0 —— TemporalClass::Detach 空指针

```asm
0x71ADE0  MOV byte ptr [[param_1+0x28]+0x270],0   ; 第一行就解引用 this->Target
```

`Target == null` 即崩溃（Phobos `TemporalClass_Release_SlaveTargetFix` 注释：
"Fixes an edge case crash caused by temporal targeting enslaved infantry"）。

### C. 0x71B151 —— Fire 释放目标时驻军未处理

原版 `TemporalClass::Fire` 释放目标时，**OpenTopped 建筑驻军的 TemporalImUsing 不释放**。
Phobos `TemporalClass_Fire_ReleaseTargetTarget` 补上：
遍历驻军让 `pTemporal->LetGo()`。

## 用户场景的最可能路径

步兵 Enter 中 → 房子被 warp out → **步兵的 Enter 目标/引用指向已消失的房子**：

1. 若步兵同时被超时空瞄准 → 命中 **A**（0x51BB7A 悬垂调用）
2. 若步兵 Enter 任务的目标过期通知未走干净（Phobos 另有
   `FootClass_PointerExpired_RemoveDestination @ 0x4D9A1B` 修同类"野指针"）
   → 下一帧 Enter 逻辑访问悬垂
3. TemporalClass 清理链中 Target 已空 → 命中 **B**（0x71ADE0）

三者共享根因：**Temporal warp out 不走正常"建筑死亡→Detach→目标失效通知"路径，
依赖引用清理的对象（正在 Enter 的步兵 / TemporalClass 自身）指针残留**。

## 修复方案分析

### 为什么不能简单"让超时空走正常死亡流程"

超时空移除的**语义就是"凭空消失"**，而正常死亡流程会带来玩家不该看到的副作用：

```
正常死亡：死亡动画 → 废墟残骸 → 被攻击警告(BuildingUnderAttack)
         → AI"我被拆了"的反应 → 废墟仍占格子
超时空：  无动画无残骸无警告，建筑直接消失（玩家预期就是这个）
```

让 warp out 改走"致死路径"会改变玩法表现（房子变废墟/触发警告），
所以原版开发者刻意保留了这条捷径——问题不在捷径本身，在**捷径的善后不全**。

### 正确方向：补全捷径的善后

原版捷径其实**做了部分善后**，只是没做全：

| 原版捷径已做的 | 原版捷径漏掉的 |
|---|---|
| `KillOccupants(0)` 杀已驻军（0x71A9A7） | Enter 中（还没进屋）的步兵 |
| `[vtable+0xf8]` 调 Detach 广播（0x71AA4F） | 通知链未覆盖全部引用者（步兵的 Enter 目标指针） |
| 移除后通知超时空单位（0x71AAD5） | `this->Target` 在移除**之后**才清（0x71AAE7），窗口期持死指针 |

补全方案（mod 源码层面）：

```
移除前：广播"建筑即将失效"给所有引用者（含 Enter 中的步兵——清其 Enter 目标/Radio 链）
移除中：TemporalClass 先清 this->Target 再移除目标（消除窗口期）
移除后：校验无残留引用
```

### Phobos 的现实修法（补丁式）

Phobos 是二进制 hook 平台，**无法重构原版控制流**，只能在三个崩溃点注入防护
（指针有效性校验）。能防崩溃，但"Enter 步兵引用残留"根因仍在——只是不再炸。

### 修复路径对比

| 方案 | 手段 | 效果 | 代价 |
|---|---|---|---|
| 补丁式（Phobos） | 崩溃点注入防护 | 不崩 | 根因残留 |
| 补全善后 | hook 移除点，手动走完整通知链 | 根治 use-after-free | 需要 mod 源码层面开发 |
| 走正常死亡 | 移除改为致死 | 根治但语义变化 | 玩法表现改变，不可取 |

## 精确确认方法

复现时抓崩溃地址（任选其一）：

1. 弹窗"应用程序错误"对话框里的指令地址（如 `0x0051BB7D`）
2. 事件查看器 → Windows 日志 → 应用程序 → 崩溃条目（异常代码 `0xC0000005` + 地址）
3. x64dbg/OllyDbg 附加运行，触发后看 EIP + 调用栈

拿到地址后与本报告的候选点对照，即可锁定唯一崩溃点。

## 取证文件

| 文件 | 内容 |
|---|---|
| `memory/data/decomp/temporal_crash_decomp.txt` | Fire/Detach/LetGo/CanWarpTarget/RemoveOccupants/KillOccupants 反编译 |
| `memory/data/decomp/temporal_ai_asm.txt` | TemporalClass::Update (0x71A760) 全汇编 |
| `memory/data/decomp/temporal_targeting_me_asm.txt` | 0x51BB40-0x51BBD0 步兵 AI 段汇编 |
| `code/ghidra_scripts/decompile_temporal_crash.py` | 反编译脚本 |
| `code/ghidra_scripts/dump_temporal_asm.py` | 汇编导出脚本 |
| `code/ghidra_scripts/dump_temporal_targeting_me.py` | 步兵段汇编脚本 |

## 交叉验证来源

- Phobos `src/Misc/Hooks.BugFixes.cpp`：`TechnoClass_AI_TemporalTargetingMe_Fix`
  (0x51BB6E/0x43FCF9/0x414BDB/0x736204)、`TemporalClass_Release_SlaveTargetFix` (0x71ADE4)、
  `TemporalClass_Fire_ReleaseTargetTarget` (0x71B151)、
  `FootClass_PointerExpired_RemoveDestination` (0x4D9A1B)
- Phobos `src/Ext/Techno/Hooks.cpp`：`TemporalClass_Update_WarpAwayAnim` (0x71A8BD)
- Phobos `src/Misc/Hooks.BugFixes.cpp`：`TemporalClass_Update_DistCheck` (0x71A7BC)
- YRpp `TemporalClass.h`：Owner@0x24 / Target@0x28 / WarpRemaining@0x48 / WarpPerStep@0x4C

## 未确认

- 精确崩溃点是 A/B/C 中的哪一个（需要运行时地址）
- `vtable+0xf8`（移除目标）与 `vtable+0x408`（驻军计数）的符号名（vtable 探测未找到字面量，
  由调用上下文推定）
