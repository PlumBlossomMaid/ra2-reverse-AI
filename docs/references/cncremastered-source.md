# EA 开源 C&C 源码——YR 逆向的对照实现（外部参照）

> **来源**：EA 开源仓库 https://github.com/electronicarts/CnC_Remastered_Collection
> **开源**：EA 2020 年发布 C&C 重置版时一并开源，GPL v3
> **含**：`REDALERT\`（红警 1，534 文件）+ `TIBERIANDAWN\`（C&C1，314 文件）
> **策略**：**只参照不复制**——本仓库不收录其代码，仅记录对应关系与差异

## 为什么它对 YR 逆向有价值

RA2/YR 引擎是 RA1 引擎的**演化而非重写**——核心类层次和大量机制
在 RA1 中已有实现，可作为 YR 二进制逆向的"参考答案"：
逆向 YR 某个机制时，先看 RA1 对应实现，能极大加速理解。

## 已验证的对应关系（2026-08-05，grep 实证）

### 类层次同源

| RA1 源码 | YR（本仓库已逆向） | 对应程度 |
|---|---|---|
| `ABSTRACT.H:49 class AbstractClass` | YR `AbstractClass` | 结构继承，但 **YR 加了 `: IPersistStream`**（存档差异，见下） |
| `FACTORY.H:40 class FactoryClass` | YR `FactoryClass`（生产系统已逆向） | 类存在，接口不同（RA1 无 AI 成员函数体系） |
| `UNIT.H` / `FOOT.H` / `TECHNO.H` / `BUILDING.H` / `INFANTRY.H` / `AIRCRAFT.H` | YR 同名单词类 | 继承链一致：Unit→Foot→Techno→Abstract |

### 生产系统前身（Time_To_Build）

| RA1 源码位置 | 内容 |
|---|---|
| `TECHNO.CPP:665 TechnoClass::Time_To_Build()` | 返回 `Class_Of().Time_To_Build()` |
| `TECHNO.CPP:6497 TechnoTypeClass::Time_To_Build()` | 类型级建造时间 |
| `OBJECT.CPP:2273 ObjectTypeClass::Time_To_Build()` | 基类实现 |

→ YR `TechnoClass::TimeToBuild`（0x70CD10 区已逆向）与其是**同源演化**，
RA1 版本可辅助理解 YR 反编译里的分支逻辑。

### 威胁系统前身（Threat）

| RA1 源码位置 | 内容 |
|---|---|
| `DEFINES.H:922 ThreatType` 枚举 | 威胁扫描方式位掩码（NORMAL/RANGE/AIR/INFANTRY…） |
| `TECHNO.H:330 virtual Greatest_Threat(ThreatType)` | 虚函数——**正是 YR 威胁系统的雏形** |
| `TECHNO.CPP:2115 TechnoClass::Greatest_Threat(method)` | 核心实现 |
| `FOOT.CPP:1943` / `UNIT.CPP:4653` / `INFANTRY.CPP:2420` / `BUILDING.CPP:2496` / `VESSEL.CPP:1251` | 各派生类覆盖链（Unit→Foot→Techno） |
| `HOUSE.H:740 Adjust_Threat(region, threat)` / `REGION.H` | 区域威胁值维护（类似 YR 威胁地图前身） |

→ YR 威胁评估系统（ThreatCoefficients + GreatestThreat 已全量逆向）的
**原型在 RA1 就有**；注意 RA1 无 ThreatCoefficients 五维公式的直接对应
（名称/结构不同），YR 部分属于增强。

### 网络/联机

- `WOLAPIOB.CPP`（Westwood Online 接口）——YR 的 WOL 模块前身

## 已验证的差异（不可照搬的部分）

### 存档系统：架构完全不同 ⚠️

| | RA1 | YR |
|---|---|---|
| 抽象 | `Pipe` / `Straw` 流（`SAVELOAD.CPP`：`Save_Game` / `Put_All` / `DLLSave(Pipe&)`） | **OLE CFB 复合文档 + COM `IPersistStream` + SwizzleManager** |
| 对象序列化 | `FactoryClass::Save(Pipe&)` 等手写逐字段 | `AbstractClass::Save` = 写 [this 地址] + [整个内存镜像] |
| 结论 | — | **YR 的存档架构是 RA2 时代重写的**，RA1 源码对存档逆向无直接帮助 |

### 其他可能差异

- RA1 无超时空（Temporal）机制的直接对应（Temporal 是 RA2 引入）
- RA1 类接口大量为旧命名（`Time_To_Build` vs YR `TimeToBuild`），
  对照时需注意命名漂移

## 使用方式

1. 逆向 YR 机制遇阻时，先 grep RA1 同名类/函数（在线浏览：
   https://github.com/electronicarts/CnC_Remastered_Collection/tree/master/REDALERT ）
2. RA1 实现只作**理解辅助**，最终结论仍以 YR 反编译 + 汇编核对为准
3. 若需引用其代码到本仓库，受 GPL v3 约束，必须保留版权头并开源

## 未完成

- 逐机制建立 RA1↔YR 对照表（当前仅 3 个已逆向机制完成对照）
- RA1 中 `CellClass::Adjust_Threat` 与 YR 威胁地图（ThreatPosedEstimates）的
  精确差异分析
