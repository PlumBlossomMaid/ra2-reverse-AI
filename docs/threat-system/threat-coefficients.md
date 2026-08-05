# ThreatCoefficients —— 目标威胁评估公式

对应原版地址 `0x70CD10`。这是索敌系统的心脏：给定"我"和"目标"，
计算目标对我的威胁值。经**汇编逐条还原**（见 `memory/data/decomp/threatcoeff_asm.txt`）。

## 公式

```
ThreatCoefficients(me, target, loc) =
    coeff.myEffectiveness     × myWarhead.Verses[targetArmor]     (1) 我能打它多少
  + coeff.targetEffectiveness × targetWarhead.Verses[myArmor]    (2) 它能打我多少
  + coeff.targetSpecialThreat × targetType.SpecialThreatValue    (3) 特殊威胁
  + (异阵营 ? EnemyHouseThreatBonus : 0)                          (4) 阵营加成
  + coeff.targetStrength      × (target血量 / 最大血量)           (5) 目标强度
  + max(距离 − 射程/256, 0)   × coeff.targetDistance             (6) 距离惩罚
```

- `Verses[armor]`：弹头对装甲的伤害倍率表（WarheadType+0xA0 起，double 数组）
- 距离 = 3D 欧氏距离（原版用快速近似 sqrt `FUN_004CAC40`）
- 射程 = 主武器 Range（weapon+0xB4）÷ 256；无武器时用 Type+0x5B8

## 汇编证据（关键指令）

| 偏移 | 指令 | 对应公式项 |
|---|---|---|
| `0x70CD1C` | `GetType()` → EBX | 加载我的类型 |
| `0x70CD48` | `[Type+0x1FB]` 标志判断 | 系数选择分支 |
| `0x70CD58` | 读 `Type+0x2C8/2D0/2D8/2E0/2E8` | 类型覆盖系数（5 个 double） |
| `0x70CDC3` | 读 `Rules+0x1068/1070/1078/1080/1088` | **Dumb 默认**系数（5 个 double） |
| `0x70CEB2` | `FMUL warhead+0xA0+idx*8` | 公式 (2)：目标弹头 × Verses |
| `0x70CEB9` | `FCHS`（target 瞄准我时） | 公式 (2) 取负 |
| `0x70CEE0` | `FMUL [Type+0x2C0]` | 公式 (3)：SpecialThreatValue |
| `0x70CF13` | `FADD Rules+0x1090` | 公式 (4)：EnemyHouseThreatBonus |
| `0x70CF49` | `FMUL warhead+0xA0+idx*8` | 公式 (1)：我的弹头 × Verses |
| `0x70CF5A` | `CALL 0x5F5C60`（血量比例） | 公式 (5) 的强度输入 |
| `0x70D0BC` | `FMUL [系数]`（距离差） | 公式 (6)：距离惩罚 |

## 系数选择机制

```
me->Type+0x1FB == 0  →  使用 Rules Dumb 默认 (0x1068 起)
me->Type+0x1FB != 0  →  使用类型覆盖系数 (Type+0x2C8 起)
```

RulesClass 偏移（Read_General 反编译确认）：

| 偏移 | 字段 |
|---|---|
| `+0x1040` | MyEffectivenessCoefficientDefault |
| `+0x1048` | TargetEffectivenessCoefficientDefault |
| `+0x1050` | TargetSpecialThreatCoefficientDefault |
| `+0x1058` | TargetStrengthCoefficientDefault |
| `+0x1060` | TargetDistanceCoefficientDefault |
| `+0x1068` | **Dumb**MyEffectivenessCoefficient |
| `+0x1070` | **Dumb**TargetEffectivenessCoefficient |
| `+0x1078` | **Dumb**TargetSpecialThreatCoefficient |
| `+0x1080` | **Dumb**TargetStrengthCoefficient |
| `+0x1088` | **Dumb**TargetDistanceCoefficient |
| `+0x1090` | EnemyHouseThreatBonus |

> 注：代码中只观察到 Dumb 默认被使用（ThreatCoefficients 默认分支）。
> 非 Dumb 默认（0x1040-0x1060）的使用点未在本次逆向范围内发现。

## 关键语义

- **Dumb = 无威胁感知能力单位**用的低权重系数套
- 公式 (6) 距离惩罚系数应为**负值**（距离越远威胁越低）
- 公式 (2) 中"目标正瞄准我"（`target->Target == me`）时取负——
  语义未完全确认（可能表示已被发现的目标降低评估优先级）
- 调用方（CanAutoTargetObject）对结果做半偶舍入转 int

## 验证（demo_threat.cpp，相对断言）

| 场景 | 结果 |
|---|---|
| 异阵营 + 双方武器：威胁 > 150 | ✅ 161 |
| 距离 512 > 射程：威胁下降 | ✅ 161 → 109.9 |
| 异阵营有加成、同阵营无 | ✅ |
| SpecialThreatValue 加成 | ✅ 661 |
| 类型覆盖系数生效 | ✅ 251 |
