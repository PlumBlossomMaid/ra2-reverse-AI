# 威胁评估系统逆向

RA2 的**索敌与威胁感知系统**——决定"单位打谁、AI 怎么判断威胁"的完整链路。
本目录记录对原版 `gamemd.exe` 中威胁系统的逆向，从反编译证据到可编译算法重写。

> YRpp 自己都留了句注释：`CalculateThreat() // this is another gem of a function, to be revealed another time...`
> ——"珍宝函数，日后揭晓"，社区 20 年没人揭。**2026-08-05 揭晓**。

## 系统全景

威胁系统分两条链路：

```
链路A: 单位威胁贡献 (ThreatPosed) —— 单位有多"危险"
  单位/建筑 → CalculateThreat() → UpdateThreatInCell() → 格子威胁
  格子威胁 → CellClass::UpdateThreat() → HouseClass::ThreatPosedEstimates[130][130]
  HouseClass::AdjustThreats() → 3×3 邻域模糊扩散 (AI 只知道大概区域)

链路B: 目标威胁评估 (索敌) —— 单位认为谁威胁最大
  GreatestThreat() ── 分层扫描 (建筑→单位→飞机→螺旋格子)
    └── CanAutoTargetObject() ── 合法性检查 + 威胁值计算
          └── ThreatCoefficients() ── 五维权重公式
```

**"AI 又聪明又笨"的代码根源**：
- 聪明：索敌分层扫描 + 射程内螺旋近处优先 + 威胁地图记忆兵力分布
- 笨：威胁评估是固定权重查表公式，行为完全模式化

## 文档

| 文档 | 内容 |
|---|---|
| [threat-coefficients.md](threat-coefficients.md) | **核心公式**：ThreatCoefficients 五维权重 + 汇编级还原 |
| [threat-map.md](threat-map.md) | 威胁地图：CalculateThreat / UpdateThreatInCell / AdjustThreats |
| [targeting.md](targeting.md) | 索敌流程：CanAutoTargetObject / TryAutoTargetObject / GreatestThreat |
| [address-map.md](address-map.md) | 函数地址 + 数据结构 + RulesClass 系数偏移 |

## 代码与验证

- 算法实现：`../../code/rewrite/threat_system.h/cpp`
- 数值测试：`../../code/rewrite/demo_threat.cpp`（14 项断言全部通过）
- 原始取证：`../../memory/data/decomp/`（threat_decomp / autotarget_decomp / threatcoeff_asm 等）

## 核心结论速览

- **单位威胁值** = `Type->ThreatPosed`（rules.ini 属性）；**建筑威胁值** = `载员数 × ThreatPerOccupant`（原版 10）
- **目标威胁公式** = `MyEffectiveness×我打目标伤害 + TargetEffectiveness×目标打我伤害 + TargetSpecialThreat×SpecialThreatValue + 异阵营EnemyHouseThreatBonus(400) + TargetStrength×血量比例 + max(距离−射程,0)×TargetDistance`
- **系数选择**：`Type+0x1FB` 标志决定用类型覆盖系数还是 Rules Dumb 默认
- **原版数值已确认**（rulesmd.ini）：非 Dumb = 200/−200/200/−200/−10；Dumb = 200/200/200/200/−1；EnemyHouseThreatBonus=400
- **威胁值 = 攻击优先级评分**：负数项揭示 AI 优先打"打不动自己、血薄、就近"的目标
- 游戏内建默认值 = 0（RulesClass 构造函数清零），实际值全部来自 rules.ini
- Phobos `Hooks.cpp:2004` 交叉验证 CalculateThreat 的建筑分支

## 待确认

- `target->Target == me` 时目标反击贡献取负的精确语义（现象已确认：威胁降为负值）
- `Type+0x1FB` 标志的确切名称/设置时机
