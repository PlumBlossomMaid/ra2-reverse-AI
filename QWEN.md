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
  YR 存档格式第一层（.sav = OLE CFB 复合文档 + SavegameInformation 属性 + CONTENTS 序列化主流程
  0x67CEF0，文档 docs/save-game/）
- **候选机制**：弹道伤害 `MapClass::DamageArea`（0x489000 区 22 hook）、
  采矿 `UnitClass`（0x73D000 区 13 hook）、寻路 `MapClass::Update_Pathfinding_1/2`（0x56C510/0x586990）
- **待确认**：`DemandProduction` 第三参数语义；`Unsuspend` 资金不足时挂起标志的行为；
  多工厂海军/陆军计数差异；`target->Target==me` 时反击贡献取负的语义；`Type+0x1FB` 标志名称
- **集中处理（挂起）**：跑游戏复现"超时空移除可驻军建筑"崩溃——事件查看器抓偏移量
  （`Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000}`），
  对照 docs/bug-triage/ 三个候选点（0x51BB7A / 0x71ADE0 / 0x71B151）精确锁定；
  复现配置：盟军 + 步兵进驻中立房子（未完成时）+ 超时空军团兵
- **存档系统**：第一层完成（外壳/SavegameInformation/保存主流程）；第二层完成
  （加载主流程 0x67E440 对称还原、批次列表、SwizzleManager 重映射、对象数组=全局向量）；
  第三层完成（对象格式 = [保存时地址]+[原始内存 dump]；UnitClass::Save 调用链
  0x744600→FootClass(0x4DB690)→TechnoClass(0x70C250)→AbstractClass(0x410320)；
  SwizzleManager 8 字节映射 {原地址,新对象}；**V3 核弹篡改点 = 单位镜像 +0x670 Type 地址**）
  **待验证**：SwizzleManager::Process(0x6CF350) 对无条目地址的处理；实际篡改实验
  （diff 两局 CONTENTS 定位修改点，需要开游戏）；CONTENTS 头部逐字节解析
- **取证数据**：反编译/汇编 21 文件在 `memory/data/decomp/`；原版 rulesmd.ini 在
  `memory/data/rules/`（威胁系数行号 500-513）；`.sav` 解析脚本 `code/analyze_sav.py`
