# 索敌流程 —— 单位怎么选目标

RA2 索敌的完整流程：`GreatestThreat` 分层扫描 + `CanAutoTargetObject` 逐目标评估。

## GreatestThreat @ 0x6F8DF0 —— 索敌主函数

```
GreatestThreat(this, threatFlags, coords, onlyTargetHouseEnemy):
    bestTarget = null, bestThreat = -1

    ── 层1: 建筑数组 (DAT_00A8E3A0) ──
    for each building:
        if CanAutoTargetObject(building, &threat) && threat > bestThreat:
            bestTarget = building, bestThreat = threat

    ── 层2: 单位数组 (DAT_00A8EC88) ──
    for each unit:
        (同上, 附带友军/无武器等过滤)

    ── 层3: 飞机跟踪器 (AircraftTrackerClass) ──
    防空单位专用: 遍历空中目标

    ── 层4: 射程内螺旋扫描格子 ──
    radius = 射程/256 + 1
    for dist in 1..radius:               // 由近到远
        for each cell on square ring:    // 螺旋
            target = cell 上的目标
            if CanAutoTargetObject(target, &threat):
                更新 bestTarget/bestThreat
            if (dist == radius/2 || dist == radius): return bestTarget  // 中途提前返回
    return bestTarget
```

**分层扫描**：先建筑后单位再飞机，最后螺旋扫格子——近处优先，
中途（半径 1/2 处）找到目标就提前返回（原版优化）。

## CanAutoTargetObject @ 0x6F7CA0 —— 目标评估核心

```
bool CanAutoTargetObject(this, target, threatFlags, ..., int* outThreat):
    ── 合法性检查 (数十项, 代表性) ──
    - 目标非空、非自己
    - 目标存活且可攻击 (WhatAmI 类型检查)
    - 友军/隐形/射程/移动类型过滤
    - 防空要求 (目标在空军跟踪器内)
    - 目标无武器时: 特殊处理 (Dumb 判断)

    ── 威胁值计算 ──
    threat = round( ThreatCoefficients(this, target) )

    ── 类型修正 (原版威胁标志) ──
    if me 类型 +0x394 == 1 (特殊单位):
        if 目标无载员: threat /= 2
        else if 载员 > 上限/2: threat *= 2
    if 目标是建筑:
        +0x800/0x8000 → threat += 内部单位数 × 1000
        +0x10000      → threat += 1000
        +0x1000       → 无内部单位 → threat = 0
    if ShouldSuppress: threat 覆盖 (放空炮抑制)

    threat = max(threat, 1)      // 最小威胁 1
    *outThreat = threat
    return true
```

**威胁最低 1**——任何可攻击的目标都有 1 点基础威胁。

## TryAutoTargetObject @ 0x6F8960

扫描格子内的目标链表（CellClass 的 `+0xE4/+0xE8` 对象链），
对每个候选调用 CanAutoTargetObject，返回第一个可攻击目标。
`GreatestThreat` 的螺旋扫描用它。

## WeaponTypeClass::AllowedThreats @ 0x772A90

```
uint AllowedThreats(WeaponType* w):
    flags = 0
    if Warhead[+0x2A4] (防空弹头):  flags |= 4       // 可打飞机
    if Warhead[+0x2A5] (反隐形?):   flags |= 0xB8    // 可打特殊目标
    return flags
```
武器根据弹头属性声明自己能打的目标类别，参与索敌过滤。
（Phobos 在此挂钩 `AAOnly` / `AU` 扩展。）

## 威胁标志位速查

| 位 | 含义（本次逆向观察） |
|---|---|
| 0x1 / 0x2 | 索敌类型选择（建筑/单位） |
| 0x4 | 飞机 |
| 0x8 / 0x50 | 特殊目标 |
| 0x800 / 0x8000 | 建筑内部单位威胁修正 |
| 0x10000 | 建筑固定 +1000 |

## 验证（demo_threat.cpp）

| 场景 | 结果 |
|---|---|
| 目标可攻击判定 | ✅ |
| 威胁值下限 = 1 | ✅ |
| 无武器同阵营威胁 = 1 | ✅ |
| 建筑载员 ×1000 修正 | ✅ 3000 |
