# RA2 生产系统算法逆向重构

对 **命令与征服：红色警戒 2 尤里的复仇**（gamemd.exe）生产/建造系统的逆向工程。
将原版二进制中的核心算法还原为可读、可编译、数据驱动的 C++ 代码。

## 背景

- **目标二进制**：`gamemd.exe`（2001-10-31 原版，MSVC 6.0，x86，ImageBase `0x400000`）
- **逆向工具**：Ghidra 11.1 headless 反编译 + 逐条汇编核对
- **符号体系**：YRpp（Ares-Developers/YRpp）地址标注 + Phobos hook 注入点交叉验证

本工程不修改游戏，也不复刻游戏引擎——只把游戏**内部算法**提炼成语言无关的逻辑，
供社区参考、复现或移植。

## 文件结构

```
rewrite/
├── CMakeLists.txt            # 算法库 + 测试目标
├── production_system.h/cpp   # 生产系统（数据结构 + 算法 + 最小数据模型）
├── threat_system.h/cpp       # 威胁评估系统
└── test/
    ├── test_production.cpp   # 生产系统 Google Test 用例（6 组）
    ├── test_threat.cpp       # 威胁系统 Google Test 用例（3 组）
    └── CMakeLists.txt        # （并入上级）
```

## 覆盖的算法

### FactoryClass 生产状态机

| 原版地址 | 函数 | 要点 |
|---|---|---|
| `0x4C98B0` | FactoryClass 构造函数 | 状态初始化、注册到全局数组 |
| `0x4C9C60` | `HasProgressChanged` | 读取并清零进度变化标志 |
| `0x4C9C70` | `DemandProduction` | 立即生产 / 入队 / 拒绝三路分支 |
| `0x4C9E60` | `Suspend` | 挂起：Rate 清零 + 计时器重置 |
| `0x4C9EA0` | `Unsuspend` | 恢复：重算 Rate 与每步成本，资金检查 |
| `0x4C9FB0` | `GetBuildTimeFrames` | `TimeToBuild()/54`，钳制 `[1, 255]` |
| `0x4C9FF0` | `AbandonProduction` | 退款 = 实际成本 − 剩余欠款 |
| `0x4CA120` | `GetProgress` | 返回当前进度值 |
| `0x4CA130` | `IsDone` | 进度达 54 判定 |
| `0x4CA180` | `GetCostPerStep` | **每步扣款 = Balance/(54 − value)** |
| `0x4CA1A0` | `CompletedProduction` | 完成时清空生产物，标记挂起等待部署 |
| `0x4CA5A0` | `StartProduction` | 出队并创建生产物 |
| `0x4CA620` | `RemoveOneFromQueue` | 队列线性查找 + 左移删除 |
| `0x4CA670` | `CountTotal` | 生产中 + 队列中计数 |
| `0x4CA6B0` | `IsQueued` | 队列存在性检查 |

### TechnoClass::TimeToBuild `@ 0x6F47A0`

建造总帧数计算公式（逐步汇编还原，见 `production_system.cpp` 注释）：

```
frames = 基础帧数                        // Type 虚函数链 (GetBuildTime)
       × 难度倍率                        // FUN_0050C0A0: 按 WhatAmI 查 House 难度表
       × 类型速度系数                    // Type+0x608
       ÷ 电力系数                        // 见下
       × MultipleFactory ^ (工厂数-1)    // 每额外工厂乘一次
       × WallBuildSpeedCoefficient       // 仅围墙 (Building 且 isWall)

电力系数 f = 1 − (1 − power) × LowPowerPenaltyModifier
   f = max(f, MinLowPowerProductionSpeed)
   power < 1.0 时: f = min(f, MaxLowPowerProductionSpeed)
   f == 0 时: f = 0.01 (防除零, 原版常量 0x7F4E34)
```

原版 `rules.ini` 默认值（社区公认）：`MinLowPowerProductionSpeed=0.5`,
`MaxLowPowerProductionSpeed=1.0`, `LowPowerPenaltyModifier=1.0`,
`MultipleFactory=1.0`。

**验证结果**（base=360 帧，难度 1.0）：

| 场景 | 期望 | 实测 |
|---|---|---|
| 满电 | 360 | 360 ✅ |
| 半电 | 720（2×） | 720 ✅ |
| 断电 | 720（下限 0.5） | 720 ✅ |
| 90% 电力 | 400 | 400 ✅ |
| 3 工厂 × 0.95 | 325 | 325 ✅ |
| 围墙 × 2.0 | 720 | 720 ✅ |

### HouseClass::GetPowerPercentage `@ 0x4FCE30`

```
available < drain 且 drain≠0:
    available≠0 → available/drain (double)
    available==0 → 0.0
否则 → 1.0
```

## 关键设计决策

1. **数据驱动**：所有依赖 rules.ini 的数值不硬编码。`BuildRules` 由外部注入，
   `BuildRules::LoadFromIni` 提供 INI 加载入口（缺省回退值也由调用方传入）。
   `FactoryClass` 构造时注入 `BuildRules`——原版直接读全局 `RulesClass::Instance`，
   本重构将其外置。
2. **半偶舍入**：原版 `FUN_007C5F00` 使用 x87 `FRNDINT`（默认 RC=0，ties-to-even），
   重写用 `std::nearbyint` 保持一致。
3. **最小数据模型**：`HouseClass`/`TechnoClass` 等只保留算法所需字段
   （字段偏移对应原版布局），真实游戏内嵌时由宿主替换为完整类型。

## 逆向方法论

1. YRpp 符号表对号入座 → 定位 FactoryClass 全部成员函数
2. Ghidra headless 导出反编译（`ghidra_scripts/decompile_factory.py`）
3. 对模糊点（TimeToBuild 的浮点数学）dump 汇编逐条还原
4. 全局常量（1.0f/0.01f/double 1.0）用 PE 解析器直接读 `.rdata` 原始字节验证
5. `RulesClass::Read_General` 反编译把偏移映射回 rules.ini 字段名
   （`MinLowPowerProductionSpeed` 等）
6. C++ 重写 + 数值测试闭环

## 待确认事项

- `DemandProduction` 第三个参数 `startNow` 的语义与原版 `shouldQueue` 的对应关系
  （原版条件分支较绕，重写按行为等价处理）
- `Unsuspend` 在资金不足时返回 false 但入口已清除挂起标志——按原样转写，
  调用方是否重新挂起取决于宿主
- 多工厂对**不同类型工厂**（步兵/载具/飞机/造船）的计数差异未细分，
  原版 `GetFactoryCount(rtti, isNaval)` 含海军/陆军区分

## 编译与测试

```
# 首次: 拉取 third_party 子模块 (gtest)
git submodule update --init --recursive

# CMake 构建 + 测试 (测试框架: Google Test)
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# 关闭测试仅编译算法库
cmake -S . -B build -DRA2_BUILD_TESTS=OFF
```

## 版权与许可

本工程是逆向研究产物，不含任何原版游戏代码或素材。地址信息来源于社区公开的
YRpp 项目（GPL）。重新发布的算法逻辑请遵守相关游戏社区规范。
