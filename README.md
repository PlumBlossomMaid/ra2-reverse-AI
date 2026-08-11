# ra2-reverse-AI

用 **Qwen Code**（AI 编程助手）对《命令与征服：红色警戒 2 尤里的复仇》进行逆向工程的
完整资产库——文档、可编译代码、CI、以及逆向过程的知识沉淀。

> 目标二进制：`gamemd.exe`（2001-10-31 原版，MSVC 6.0，x86，ImageBase `0x400000`）

## 为什么做这个

- 社区已有 20 年逆向成果（YRpp 符号体系、Ares/Phobos 扩展），但**原版二进制 + 符号 + 反编译 + 算法重写**的完整链路鲜有公开
- 目标不是分发复刻游戏，而是**独立重写机制（行为对齐）、回馈社区**——像一个可以持续扩展的"游戏机制百科"
- 本仓库同时记录 **AI 辅助逆向的工作流**：符号标注 → 反编译 → 汇编核对 → 算法重写 → 数值验证

## 愿景（2026-08-06）

**三层目标**——当前进行中的模块：遭遇战随机地图生成器逆向：

1. **完美复刻（本质）**：不是"新写一套能用就行"的随机地图生成程序，而是**字节级复现原版逻辑**——同一种子 → 同一张 .map（哈希对比验证）。生成地图只是副产品
2. **为重写铺路（进阶）**：为"完美重写红警2"（引擎级 reimplementation，OpenRA 式黑盒行为对齐 + 独立实现）积累子系统拼图——生产系统、威胁评估、游戏循环、存档格式、随机地图生成器……本模块是第一个交付条件成熟的基石
3. **回馈社区（愿景）**：社区有 XCC（.map 读写）、Ares/Phobos（mod 扩展），但**字节级复现原版随机地图生成逻辑的独立工具是空白**——开源即填补

> **对齐标准提醒**：原版生成器允许"不平衡/偶尔离谱"的地图（孤岛开局、半张废图）是**原版口味**——复刻出孤岛是**对齐成功的证据**，不是 bug；反之原版出孤岛而我们出不了，说明约束规则抄漏了。

## 目录结构

```
ra2-reverse-AI/
├── README.md            ← 本文件：入口与导航
├── QWEN.md              ← Qwen Code 工作手册（索引、工作流、规范）
├── CMakeLists.txt       ← 顶层构建（MSVC /utf-8, RA2_BUILD_TESTS 开关）
├── third_party/         ← 第三方依赖（git submodule, 架构参考 PaddlePaddle）
│   └── gtest/               # Google Test v1.14
├── docs/                ← 文档区：逆向分析文章
│   ├── production-system/   # 生产系统（已完成全量逆向）
│   ├── threat-system/       # 威胁评估系统（已完成全量逆向，含"珍宝函数"）
│   ├── game-loop/           # 游戏运行流总览（已完成：入口→选关→单局→胜负→退出）
│   ├── cd-key/              # YR 正版校验机制（woldata.key + CD 门禁 + -CD 开关，已完成）
│   ├── methodology/         # 逆向方法论（Ghidra 工作流、AI 协作模式）
│   ├── symbols/             # 符号标注成果说明
│   ├── bug-triage/          # 崩溃排查（Temporal warp 建筑崩溃）
│   └── save-game/           # YR 存档格式（OLE CFB + CONTENTS 序列化）
├── code/                ← 代码区：可编译的算法重写 + Ghidra 脚本
│   ├── rewrite/             # C++ 算法重写（CMake 库 + Google Test 用例）
│   ├── ghidra_scripts/      # Ghidra headless 脚本（标注/反编译/探测）
│   └── *.py                 # 符号解析、PE 常量读取等工具
├── ci/                  ← CI 设计与文档（workflow 位于 .github/workflows/）
├── memory/              ← 记忆区：踩坑记录、地址笔记、原始取证数据
│   ├── notes/               # 知识笔记（Ghidra Jython 踩坑等）
│   └── data/                # 原始数据（符号表、反编译输出、B站参考资料）
└── .github/workflows/   ← GitHub Actions（CMake + Google Test + ctest）
```

## 核心成果（截至 2026-08-05）

### 符号标注（三层）
- YRpp 函数名 **1106 个** + 全局变量 **251 个** + Phobos hook 注入点 **1468 个**
- 符号表：`memory/data/symbols/`

### 生产系统全量逆向（首个机制）
- **FactoryClass** 16 个成员函数反编译 + 汇编核对
- **TechnoClass::TimeToBuild** 建造时间算法汇编级还原（电力/难度/多工厂/围墙）
- C++ 重写 + **45 项数值测试全部通过**：`code/rewrite/`
- 文档：`docs/production-system/`

### 威胁评估系统逆向（YRpp 盖章的"珍宝"）
- **ThreatCoefficients** 五维威胁公式汇编级还原（`0x70CD10`）
- **CalculateThreat**（"another gem of a function, to be revealed..."——2026-08-05 揭晓）
- 完整链路：威胁地图（ThreatPosedEstimates 130×130 + 3×3 模糊扩散）→ 索敌（分层扫描）
- C++ 重写 + **14 项数值测试全部通过**：`code/rewrite/`
- 文档：`docs/threat-system/`

### 崩溃排查：超时空移除可驻军建筑（进行中）
- 场景：步兵 Enter 中 → 超时空 warp out 建筑 → 崩溃（战斗碉堡同样触发）
- **TemporalClass::Update（0x71A760）汇编级还原**：冻结完成 → KillOccupants → 移除建筑的完整链
- Phobos 交叉验证出 3 个原版崩溃点：`0x51BB7A` / `0x71ADE0` / `0x71B151`
- 待运行时崩溃地址精确命中（复现后事件查看器抓偏移量，基址固定 `0x400000`）
- 文档：`docs/bug-triage/temporal-building-warp-crash.md`

### 游戏运行流全量逆向（给后续机制绘制地图）
- **三层结构**：初始化（WinMain 0x6BB9A0）→ 会话选择（FUN_0052D9A0）→ 单局循环（MainLoop 0x55D360）
- **单局循环**：MainLoop（LogicClass::Update 逻辑心脏 + 消息列表 + 录制/回放）→ AuxLoop（结束事件分派）
- **胜负判定**：HouseClass 内部标志（+0x1F7 胜利 / +0x1F8 被消灭）→ 4 个全局结束标志 → DoWin/DoLose/DoRestart/DoExit
- **开局链**：ScenarioClass::Start（0x683AB0）→ 加载层（0x684620）→ ReadScenario_MissionINI（0x686B20）
- RA1 源码（本地 CnCRemastered）逐函数对照，结构与 `CONQUER.CPP` 同源演化
- 文档：`docs/game-loop/game-lifecycle.md`（5 轮 Ghidra 取证，21+ 地址证据链）

### YR 正版校验机制逆向（woldata.key + CD 门禁 + -CD 开关）
- **woldata.key 解密公式实锤**（FUN_005DC170 @ 0x5DC170）：注册表 `Serial` 与 key 文件
  逐字节按位减法 `(serial - key) mod 10`——**不是签名验证，解密结果不验证真伪**，
  这就是"改一个字符还能玩"的真相（游戏没有"正确序列号"可对比）
- **启动门禁**：FUN_004A8270（0x4A8270）用 `CDDriveManagerClass::GetCDNumber()`
  检查光驱里有没有游戏盘（不是序列号验签）
- **-CD 官方免检开关**：命令行参数表（0x826590 区）内置 `-CD`，检测到即写免检标志
  `0x89E3A0=1` → 全局安全检查（FUN_004790E0，11 处调用）全部短路
- **免 CD 无需 patch exe**：启动器传 `-CD` 参数即可；用户环境实证（YRLauncher.exe
  第三方启动器 + Woldata.key 112B Base64 + Ares 平台），exe 为 2001-10-31 原版字节
- 文档：`docs/cd-key/cd-key-mechanism.md`（6 轮取证，8+ 地址证据链）

### YR 存档格式逆向（第一层完成）
- **文件格式实锤**：`.sav` = OLE CFB 复合文档（魔数 `D0 CF 11 E0...`），与 .doc/.xls 同族
- **内部结构**：`CONTENTS` 主数据流（1.1-2.6MB，全部游戏状态）+ 13 个元数据流
- **SavegameInformation**（0x6812E0）：IPropertySetStorage 写 13 个 PIDSI_* 属性，魔数 `0x2B898`
- **保存主流程**（0x67CEF0）：StgCreateDocfile → 属性集 → CreateStream("CONTENTS") → COM IPersistStream 按对象数组批次序列化（OleSaveToStream）
- **实证**：2002 原版 vs 2022 mod 存档外壳完全兼容 → 跨 mod 鬼畜/V3 核弹根源在 CONTENTS 对象镜像
- 文档：`docs/save-game/sav-format.md`

### 随机地图生成器逆向（进行中，2026-08-06）
- **编译单元测绘**：调试宏反推 `D:
a2mdpost\MapGen.cpp` 代码区 **0x5956E7-0x59A63A**（21KB，源码 6746 行）
  ——工具 `code/randmap/scan_source_macros.py` 顺带产出全 exe 33 个源文件↔代码区映射
- **生成器类实锤**：`MapSeedClass`（YRpp 符号交叉验证），单例 `Instance @ 0xABDFD8`；
  17 参数偏移表本仓库独立还原后与 YRpp `MapSeedClass.h` **完全吻合**
- **生成流水线全景**：主流程 `FUN_00598960` 的调试日志自带阶段名——
  Init random map → Seeding water → Init regions → Making regions → Creating starting points
  → Adding tech buildings → Creating tiberium → Creating hills → Creating LATs/rocks → Cleanup → Done
- **随机数发生器 R250 逐位复刻完成** ✅：播种 `0x65C6D0`（4 轮 Feistel）/ 取数 `0x65C780` /
  区间随机 `0x65C7E0`（掩码+拒绝采样）；C++ 重写 + **Google Test 7 项全过**
- **复刻前提：原版全局浮点环境实锤**：WinMain `_control87(_RC_CHOP,_MCW_RC)` @0x6BBFB7
  → 舍入模式**向零截断**，缓存控制字 `[0x822D80]`=**0x0E7F**（PC=53 位 + RC=chop），
  `_ftol@0x7C5F00` 快路径比对同一值——三条证据互证
- **生成器全局 `Randomizer @ 0xABE890`**：整张地图**只播种一次**，种子取自 `.sed` 的 `Seed=`
  （`MapSeedClass+0x74`）→ "阶段顺序 + 每阶段取数个数"就是种子兼容性的全部内容
- **取数惯用法复刻** ✅：按 `this == 0xABE890` 配对，生成器共 **150 处 `Random`、
  0 处 `RandomRanged`**——只看"调了哪个函数"会把引擎自己那个 Randomizer 算进来。
  再往上追引用发现 **46 处落在零引用的死代码里**（22 个函数），真正会执行的是 **104 处**，
  已逐点归到 12 个生成阶段名下（`docs/random-map/rng-ledger.md`）——这就是复刻的施工图。
  区间随机一律走浮点比例 + 拒绝采样
  `(int)((double)(uint32)Random() * (n/4294967295.0))`；重写 + gtest 3 项全过
- **原版数学基元复刻** ✅：`FastSqrt@0x4CAC40` **不是真开方**——拆 float 位形查 16384 项表，
  只有 14 位精度（误差 5.7e-5 = 2⁻¹⁴），且表可由**纯整数** `isqrt` 重算（16384/16384 逐位相同，
  表已写死进仓库并用整数运算自检）；`NormalDeviate@0x5980C0` 是 Marsaglia 极坐标法正态分布（缓存第二个值）
- **`fyl2x` 的语义被自己的对拍推翻过一次**：80 样本得出的"逐位一致"是假象——
  Unicorn 模拟器**根本不理会 FPU 控制字**。改用真 CPU 只读探针取 20000 组真值后确认：
  真 `fyl2x` 与「精确 ln 向零截断」有 **7/20000 = 0.035%** 的 1 ulp 分歧，
  **它本身就不是正确舍入的**。故产品代码**不跟本机指令走**，改用唯一确定的数学定义
  （双双精度实现，全架构同一条路），并**证明**残余 1 ulp 被下游 14 位查表 sqrt 吸收
  （实测穿透 0 次）。gtest 10 项全过，含 101 组不经过任何硬件浮点算出的架构无关黄金值
- **陆地形状的算法已还原**：涂块核心 `FUN_0059BBC0` 是 Dijkstra 式最小堆扩散——
  优先级 = `FastSqrt(到引力中心距离)` + 均匀 `[0,5)` 抖动，
  而**引力中心每步做布朗运动**；收尾铺岸线失败则**整块抹回海里**
- **阶段 4「Making regions」落 C++ 并对拍完成** ✅：0x58EBC0 切分（0x58D620 全 4 取数点）+
  0x58EF10 重建/开通路 + 0x5A19E0 悬崖修角 + 0x578E60 全图重刷 + 0x5A17F0 多格砖重掷，
  ctest 38/38 全绿。**两处实锤**：① 0x89A304 方向偏移表是 `.data` 零填充区（全 0）→
  候选标记写"节点自己"→ **原版扩散无防重、候选重复入堆**，C++ 照抄不防重；
  ② 切分后**原区域直接销毁**（0x58EEFC DestroyRegion + restart），不是合并
- **验证方法论**：`code/randmap/emu_oracle.py` 用 Unicorn 指令级模拟跑原版机器码取标准答案，
  与独立重写逐位对拍（8 种子 × 播种状态/取数/区间随机全部一致）；
  并支持**任意代码片段执行**（给起止地址 + 寄存器 + FPU 控制字），含 x87 的惯用法即由此取真值
- 文档：`docs/random-map/`　取证：`memory/data/decomp/mapgen_tu_map.txt` / `rmg_pipeline_decomp.txt` /
  `randomizer_asm.txt` / `rmg_blob_growth.txt`　基准：`memory/data/rmg/rng_golden.txt` / `fastmath_golden.txt`

## 工作流速览

1. **符号对号入座**：YRpp 符号表 → `code/ghidra_scripts/apply_symbols.py`
2. **反编译**：`decompile_factory.py` 等 → 导出 C 伪代码
3. **汇编核对**：模糊点 dump 汇编逐条还原（`dump_timetobuild_asm.py`）
4. **常量取证**：PE 解析直接读 `.rdata` 字节（`read_constants.py`）
5. **规则映射**：`RulesClass::Read_General` 反编译 → rules.ini 字段名
6. **算法重写 + 测试**：`code/rewrite/`，CI 自动验证

详见 [QWEN.md](QWEN.md) 和 [docs/](docs/)。

## 状态

- [x] Ghidra 全量分析（8637 函数）
- [x] 三层符号标注
- [x] 生产系统机制逆向 + C++ 重写 + 测试
- [x] 威胁评估系统机制逆向 + C++ 重写 + 测试
- [x] 游戏运行流逆向（启动/选关/单局/胜负/退出全景）
- [x] YR 正版校验机制逆向（woldata.key 解密 + CD 门禁 + -CD 开关）
- [x] 崩溃排查初版（超时空移除可驻军建筑，待运行时地址确认）
- [ ] 随机地图生成器逆向（编译单元测绘 + 流水线全景完成；RNG 已逐位复刻；各阶段算法还原中）
- [ ] 更多机制挖掘（弹道伤害、采矿、寻路、LogicClass::Update 解剖）
- [ ] 发布文章整理

## 版权

本仓库不含任何原版游戏代码或素材。地址信息来源于社区公开的 YRpp（GPL）。
算法重写为独立实现，重新发布请遵守相关社区规范。
