# EA 开源 C&C 源码——YR 逆向的对照实现（外部参照）

> **来源**：EA 开源仓库 https://github.com/electronicarts/CnC_Remastered_Collection
> **开源**：EA 2020 年发布 C&C 重置版时一并开源，GPL v3
> **含**：`REDALERT\`（红警 1，534 文件）+ `TIBERIANDAWN\`（C&C1，314 文件）
> **策略**：**只参照不复制**——本仓库不收录其代码，仅记录对应关系与差异

## 为什么它对 YR 逆向有价值

RA2/YR 引擎是 RA1 引擎的**演化而非重写**——核心类层次和大量机制
在 RA1 中已有实现，可作为 YR 二进制逆向的"参考答案"：
逆向 YR 某个机制时，先看 RA1 对应实现，能极大加速理解。

### 手写汇编取证（2026-08-12 发现）

RA1/TD 源码还有一个独特价值：**源码级证实 Westwood 手写汇编传统**。
逆向 YR 时看到"怪汇编"（无 return / 位操作 / 怪默认值），可能是 C++ bug，
也可能是手写汇编——判断依据见
[handwritten-asm-forensics.md](../methodology/handwritten-asm-forensics.md)。

证据摘要：`REDALERT\` 下 27 个独立 `.asm`（图形/压缩/网络/系统全覆盖，
连 `strtrim` 都手写汇编）+ `MiscAsm.cpp` 41 处内联 `__asm`
（`Distance_Coord` 无 return，靠 EAX 传返回值）。

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

### 随机数系统前身（区间随机 = 掩码 + 拒绝采样）

| RA1 源码位置 | 内容 |
|---|---|
| `REDALERT/RANDOM.H:52` `class RandomClass` | 32 位 LCG，`MULT_CONSTANT=0x41C64E6D`、`ADD_CONSTANT=0x3039` |
| `REDALERT/RANDOM.H:61,79` | `SIGNIFICANT_BITS=15`、`THROW_AWAY_BITS=10` —— 取数只返回 15 位 |
| `REDALERT/RANDOM.CPP:91` `operator()()` | `Seed = Seed*K + C; return (Seed >> 10) & 0x7FFF;` |
| `REDALERT/RANDOM.CPP:125` `operator()(int,int)` | **区间随机**，注释原文：*"Keep picking random numbers until it fits within the magnitude desired."* |

RA1 的区间随机骨架：

```c
if (minval == maxval) return minval;
if (minval > maxval)  swap(minval, maxval);
int magnitude = maxval - minval;
int highbit = SIGNIFICANT_BITS - 1;                    // 从第 14 位往下扫
while ((magnitude & (1 << highbit)) == 0 && highbit > 0) highbit--;
int mask = ~((~0L) << (highbit + 1));
int pick = magnitude + 1;
while (pick > magnitude) pick = operator()() & mask;   // 拒绝采样
return pick + minval;
```

**这与 YR `Randomizer::RandomRanged`（`0x65C7E0`，见
[randomizer.md](../random-map/randomizer.md) §区间随机）是同一套骨架**：
求覆盖 `range` 的最小位掩码 → 反复取数直到落进区间 → 加回下界。
底层发生器从 LCG 换成了 R250、位宽从 15 位涨到 32 位，
**「掩码 + 拒绝采样 + 加下界」这三步一字未改**。

已确认的差异（都在 YR 侧，且都是位宽变大带来的）：

| | RA1 `RandomClass` | YR `Randomizer` |
|---|---|---|
| 发生器 | LCG，`Seed = Seed*0x41C64E6D + 0x3039` | R250（滞后 103/250），4 轮 Feistel 播种 |
| 有效位 | 15（`>>10 & 0x7FFF`） | 32 |
| 位扫描 | 从第 14 位往下，`highbit > 0` 兜底 | 第 31 位单独 `TEST EDI,0x80000000` 短路，循环从 31 先减后测 |
| `bits == 32` | 不可能发生 | 掩码退化成 0（x86 `SHL` 只取 `CL` 低 5 位），YR 原样保留 |
| `range+1` 溢出 | 不可能发生 | `LEA EAX,[EDI+1]; CMP EAX,EDI; JLE` 短路，直接返回 `(range+1)+lo` |

> ⚠️ 但**随机地图生成器一次都没用 `RandomRanged`**（150 处取数点全是 `Random()`）。
> `MapGen.cpp` 自带一套 `PickRange`（`0x598030`）/ `PickBelow`，
> 用的是「浮点比例 + 拒绝采样」而不是掩码：
> `v = _ftol(r * (hi-lo+1) * (1/(2^32-1)) + lo)`，再 `while ((unsigned)v > (unsigned)hi)` 重抽。
> 拒绝采样这个**习惯**一脉相承，具体**手法**是地图生成器自己另起的炉灶。

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

### RA1 参考答案的边界（随机地图生成器：完全无参考）

2026-08 逆向阶段 10 时做过一次全树排查，结论要记牢：

| 主题 | RA1 有无 | 说明 |
|---|---|---|
| 随机数发生器 | ✅ 有 | `RANDOM.H/CPP`，YR 的 `Randomizer` 就是同一套骨架 |
| `.ini` 读取 | ✅ 有 | `CCINI.CPP`，`ReadInteger` 语义可直接对照 |
| **随机地图生成器** | ❌ 无 | 全树 grep `MapSeed` / `Ruggedness` / `random map` **零命中**；RMG 是 Tiberian Sun 才有的，TS 源码未公开 |
| **LAT 自动过渡** | ❌ 无 | RA1 是 2D 正方格 + 手工枚举的 `TEMPLATE_*` 表（`CDATA.CPP`），与 `CellClass::SetupLAT` 的邻接推导不是一回事 |
| **等距地图 / 高度场** | ❌ 无 | RA1 无 Z 轴、无 `IsoMapPack5` |

所以：**「随机数 / ini / 单位逻辑 / 存档」优先查 RA1；
「等距地图 / LAT / 高度场 / RMG」只能靠反汇编硬啃**。

## 使用方式

1. 逆向 YR 机制遇阻时，先 grep RA1 同名类/函数（在线浏览：
   https://github.com/electronicarts/CnC_Remastered_Collection/tree/master/REDALERT ）
2. RA1 实现只作**理解辅助**，最终结论仍以 YR 反编译 + 汇编核对为准
3. 若需引用其代码到本仓库，受 GPL v3 约束，必须保留版权头并开源

## 未完成

- 逐机制建立 RA1↔YR 对照表（当前仅 3 个已逆向机制完成对照）
- RA1 中 `CellClass::Adjust_Threat` 与 YR 威胁地图（ThreatPosedEstimates）的
  精确差异分析
