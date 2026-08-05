# TechnoClass::TimeToBuild — 建造时间公式

对应原版地址 `0x6F47A0`。经汇编逐条还原（见 `memory/data/decomp/timetobuild_asm.txt`），
是 FactoryClass 建造速率的源头。

## 公式

```
frames = baseBuildTime                      (1) 基础帧数
       × difficultyFactor                   (2) 难度倍率
       × typeSpeedFactor                    (3) 类型速度系数
       ÷ lowPowerFactor                     (4) 电力系数
       × MultipleFactory^(factoryCount-1)   (5) 多工厂
       × wallBuildSpeedCoefficient          (6) 围墙（仅墙）

每次乘/除后 x87 半偶舍入 (FRNDINT)
```

## 逐步汇编证据

| 步骤 | 汇编 | 含义 |
|---|---|---|
| (1) | `CALL [this+0x88]` → `CALL [type+0x88]` | vtable 链取 Type 的 GetBuildTime |
| (2) | `CALL 0x50C0A0` + `FIMUL` | 难度倍率（按 WhatAmI 查 House 难度表） |
| (3) | `FMUL [Type+0x608]` | 类型速度系数 |
| (4) | `CALL 0x4FCE30`（电力）→ `FDIV` | 电力修正 |
| (5) | `GetFactoryCount` + `FMUL MultipleFactory` 循环 | 每额外工厂乘一次 |
| (6) | `FMUL qword [Rules+0x758]` | 围墙系数（double） |

## 电力系数（步骤 4 细节）

```
power = House->GetPowerPercentage()          // [0,1]
f = 1 - (1 - power) × LowPowerPenaltyModifier
f = max(f, MinLowPowerProductionSpeed)
if (power < 1.0) f = min(f, MaxLowPowerProductionSpeed)
if (f == 0) f = 0.01                          // 防除零，常量 0x7F4E34
frames = frames / f
```

原版默认值：`Min=0.5`、`Max=1.0`、`Penalty=1.0`。
**断电时 f=0.5 → 建造时间 ×2**。

## 多工厂（步骤 5 细节）

```
factoryCount = Owner->GetFactoryCount(WhatAmI, unitFlag)
if (MultipleFactory != 0 && factoryCount > 1)
    for i in 1..factoryCount-1:
        frames = round(frames × MultipleFactory)
```

`MultipleFactory=1.0`（原版默认）时无影响。`unitFlag` 来自
`Type+0xCCE`（仅 Unit 类型）。`GetFactoryCount` 原版地址 `0x500910`，
Phobos 在此挂钩扩展（`ExcludeFromMultipleFactoryBonus`）。

## 围墙（步骤 6 细节）

`WhatAmI()==Building(6)` 且 `Type+0x1571`（墙标志）时：
`frames ×= WallBuildSpeedCoefficient`（double）。

## rules.ini 字段映射（RulesClass 偏移，经 Read_General `0x66D530` 反编译确认）

| 偏移 | 字段 |
|---|---|
| `+0x570` | `[General]MinLowPowerProductionSpeed` |
| `+0x574` | `[General]MaxLowPowerProductionSpeed` |
| `+0x578` | `[General]LowPowerPenaltyModifier` |
| `+0x57C` | `[General]MultipleFactory` |
| `+0x758` | `[General]WallBuildSpeedCoefficient` |

## 全局常量（PE 实测）

| 地址 | 值 | 用途 |
|---|---|---|
| `0x7E2AC8` | 1.0f | 系数基准 |
| `0x7E1748` | 0.0f | 零值比较 |
| `0x7F4E34` | 0.01f | 防除零下限 |
| `0x7E1718` | 1.0 (double) | GetPowerPercentage 满电返回 |
| `0x7E2800` | 0.0 (double) | GetPowerPercentage 断电返回 |

## 验证（demo.cpp 用例，base=360）

| 场景 | 期望 | 实测 |
|---|---|---|
| 满电 | 360 | 360 ✅ |
| 半电 | 720 | 720 ✅ |
| 断电 | 720 | 720 ✅ |
| 90% 电力 | 400 | 400 ✅ |
| 3 工厂 × 0.95 | 325 | 325 ✅ |
| 围墙 × 2.0 | 720 | 720 ✅ |
