# Qwen Code 工作手册

本文件是 Qwen Code 在本仓库中的工作规范。每次在新会话中操作本仓库，先读本文件。

## 仓库架构

```
E:\code\ra2-reverse-AI\
├── QWEN.md              ← 本文件
├── README.md            ← 仓库入口：项目概述 + 导航
├── docs\                ← 文档区：逆向分析文章（机制解析、方法论、符号说明）
├── code\                ← 代码区：C++ 算法重写 + Ghidra 脚本 + 工具脚本
├── ci\                  ← CI 设计说明（workflow 实体在 .github\workflows\）
├── memory\              ← 记忆区：踩坑笔记 + 原始取证数据
└── .github\workflows\   ← GitHub Actions
```

## 查询流程

当需要回答逆向/机制问题时：
1. 读 `docs\` 索引，定位相关机制文档
2. 需要细节时读 `memory\data\` 下的原始反编译/汇编取证数据
3. 需要确认地址语义时查 `memory\data\symbols\` 符号表
4. 结合 `code\rewrite\` 中的算法实现给出结论

## 逆向工作流（标准流程）

1. **符号对号入座**：`code\ghidra_scripts\apply_symbols.py` 应用 YRpp 符号
   - 运行：`analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis -postScript apply_symbols.py -scriptPath E:\code\ra2-reverse-AI\code\ghidra_scripts`
2. **反编译导出**：`decompile_*.py` 系列导出指定函数 C 伪代码
3. **汇编核对**：模糊的浮点/条件逻辑 dump 汇编逐条还原
4. **常量取证**：`code\read_constants.py` 从 PE 读 `.rdata` 原始字节（注意 float/double 区分）
5. **规则映射**：反编译 `RulesClass::Read_General` 把偏移映射回 rules.ini 字段名
6. **算法重写**：`code\rewrite\` 新增模块，遵守数据驱动原则（rules 数值不硬编码）
7. **数值验证**：`code\rewrite\demo.cpp` 加测试用例，CI 自动跑

## 硬性规范

1. **数据驱动**：依赖 rules.ini 的数值一律通过 BuildRules 注入，不硬编码在算法里；
   测试程序中可以硬编码原版默认值
2. **注释克制**：不写"此地无银三百两"式注释（如"纯计算、无依赖"）；只写地址依据
   和行为依据
3. **取证优先**：断言任何地址/常量的语义前，必须用反编译/汇编/PE 字节三重验证之一；
   无法验证的标注"未确认"
4. **半偶舍入**：原版 x87 FRNDINT（ties-to-even），重写用 std::nearbyint
5. **Ghidra 脚本踩坑**：见 `memory\notes\ghidra-jython-pitfalls.md`，写新脚本必读

## 环境速查

- Ghidra：`D:\ghidra_11.1_PUBLIC_20240607\ghidra_11.1_PUBLIC\support\analyzeHeadless.bat`
- Ghidra 工程：`E:\code\ra2-reverse\ghidra_proj\RA2`（gamemd.exe 全量分析，含三层符号）
- MSVC 编译：`vcvars64.bat` + `cl /std:c++17 /utf-8 /W4`
- 原版二进制：`E:\YRLauncher\gamemd.exe`
- 交叉验证源码：YRpp `E:\code\YRpp`（GPL，Ares-Developers 克隆）、Phobos `E:\code\Phobos`（GitHub 克隆）

## 当前状态与下一步

- **已完成**：三层符号标注；生产系统全量逆向（FactoryClass + TimeToBuild + 测试 45 项全过）；
  威胁评估系统全量逆向（ThreatCoefficients 五维公式 + CalculateThreat"珍宝函数" + 威胁地图 + 索敌，
  测试 14 项全过，文档 docs/threat-system/）；
  崩溃排查初版（超时空移除可驻军建筑，TemporalClass::Update 0x71A760 汇编还原 + Phobos 交叉验证
  3 崩溃点，文档 docs/bug-triage/）；
  YR 存档格式三层（外壳/加载流程/对象格式，文档 docs/save-game/）；
  **游戏运行流全量逆向**（WinMain 0x6BB9A0 → MainGame 0x48CCC0 → 选关 0x52D9A0 →
  ScenarioClass::Start 0x683AB0 → MainLoop 0x55D360 → 胜负判定 0x4F8440 区 →
  DoWin 0x685670 / DoLose 0x685DC0，5 轮 Ghidra 取证，文档 docs/game-loop/）；
  **YR 正版校验机制全链逆向**（woldata.key 解密公式 FUN_005DC170 + CD 门禁 FUN_004A8270 +
  `-CD` 免检开关 0x52F7AF/0x89E3A0，6 轮取证，文档 docs/cd-key/）——
  "改一个字符还能玩"的真相：woldata.key 是按位减法解密、不验证真伪；
  免 CD 破解=启动器传 `-CD` 参数，exe 未被 patch（2001-10-31 原版字节）
- **候选机制**：弹道伤害 `MapClass::DamageArea`（0x489000 区 22 hook）、
  采矿 `UnitClass`（0x73D000 区 13 hook）、寻路 `MapClass::Update_Pathfinding_1/2`（0x56C510/0x586990）；
  **逻辑心脏 `LogicClass::Update`**（MainLoop 内驱动，胜负判定/全游戏更新的挂载点）
- **地图生成器（遭遇战随机地图，用户目标：命令行程序输出原版逻辑对齐的 .map）**：
  侦察完成（2026-08-06）——随机地图入口在加载层 0x684620 随机分支，但 FUN_00597A10
  只是 0x1D 字节跳板，**真身在 vtable+4 虚函数**（下轮从调用上下文定位对象类型）；
  **Randomizer（R250：250 uint 状态双索引 XOR 回绕）@ 0x65C780 完整还原** + RandomRanged
  拒绝采样 @ 0x65C7E0；**读图入口 ReadMap = FUN_00689E90 @ 0x689E90**（[Header] StartX/Width/
  Height/NumberStartingPoints、[Basic]、[Waypoint] Read2Integers）——输出端对齐目标。
  取证 `memory/data/decomp/randmap_probe.txt`；脚本 `code/ghidra_scripts/decompile_randmap.py`。
  注意：RA1 源码（CnCRemastered）**无**随机地图生成器（TS/RA2 新特性），仅底层随机数/地形可参照
- **待确认**：`DemandProduction` 第三参数语义；`Unsuspend` 资金不足时挂起标志的行为；
  多工厂海军/陆军计数差异；`target->Target==me` 时反击贡献取负的语义；`Type+0x1FB` 标志名称；
  胜负判定 0x4F8440 的调用者；house+0x1F7（胜利条件）写入点；WinMain COM 类清单
- **集中处理（挂起）**：跑游戏复现"超时空移除可驻军建筑"崩溃——事件查看器抓偏移量
  （`Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000}`），
  对照 docs/bug-triage/ 三个候选点（0x51BB7A / 0x71ADE0 / 0x71B151）精确锁定；
  复现配置：盟军 + 步兵进驻中立房子（未完成时）+ 超时空军团兵
- **存档系统**：三层完成（外壳/加载/对象格式 + V3 篡改点 = 单位镜像 +0x670 Type 地址）
  **待验证**：SwizzleManager::Process(0x6CF350) 对无条目地址的处理；实际篡改实验
  （diff 两局 CONTENTS 定位修改点，需要开游戏）；CONTENTS 头部逐字节解析
- **取证数据**：反编译/汇编 42 文件在 `memory/data/decomp/`（含 gameloop_* 5 份、
  cdcheck_* 5 份 + woldata_* 2 份）；原版 rulesmd.ini 在 `memory/data/rules/`（威胁系数行号 500-513）；
  `.sav` 解析脚本 `code/analyze_sav.py`
