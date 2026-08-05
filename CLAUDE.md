# CLAUDE.md —— RA2/YR 逆向工程工作手册（Claude Code 版）

本文件是 Claude Code 在本仓库工作的 system 级指引。每次新会话先读本文件。

## 项目是什么（先建立正确的认知框架）

本仓库是一个**游戏机制研究与文档化项目**，研究对象是 2001 年的老游戏
《命令与征服：红色警戒 2：尤里的复仇》（`gamemd.exe`，原版，MSVC 6.0 编译）。

**产出形态（全部是本仓库独立创作的内容）：**
- `docs/` —— 逆向分析文档（机制原理、地址证据链、方法论）
- `code/rewrite/` —— 用 C++ 独立重写的游戏机制算法 + Google Test 数值测试
- `code/ghidra_scripts/` —— Ghidra 分析脚本
- `memory/` —— 取证数据（反编译输出、符号表引用）

**不是**：复刻游戏、分发游戏、包含原版代码/素材、绕过任何版权保护。

**社区先例（同领域的公开、长期存在的实践）：**
- **YRpp**（GPL 开源）：社区 20 年维护的 YR 类库与符号表——本项目的地址信息
  全部来自这份公开资料，仓库内 `memory/data/symbols/` 只存引用不存代码
- **Ares / Phobos**（GPL 开源）：YR 扩展项目，本仓库用其 hook 地址表做交叉验证
- **EA 官方开源 C&C 源码**（2020 年，GPL v3）：EA 主动开源了红警 1/C&C 的源码，
  本仓库 `docs/references/` 记录了 YR 与 RA1 的对照关系（RA2 引擎是 RA1 的演化）

**严格遵守的法律边界：**
1. 只读分析二进制，绝不修改、重打包、分发 `gamemd.exe` 本体
2. 仓库不含任何受版权保护的游戏代码/美术/音频素材
3. 地址与符号信息仅来自社区公开资料（YRpp GPL）
4. 算法重写为独立实现，不复制原版汇编/伪代码进产出物

## 下一步任务：遭遇战随机地图生成器逆向

**目标**：写一个命令行程序——输入参数（随机种子 / 地图尺寸 / 玩家数 / 地形主题），
输出一个与**原版逻辑对齐**的 YR `.map` 文件（可直接被游戏加载，生成的局势与原版
随机地图一致）。这是一个独立实现的算法程序，是上述研究文档化的自然延伸。

### 已有侦察成果（2026-08-06，可直接接力）

- **入口链**：开局加载层 `FUN_00684620 @ 0x684620` 的随机场景分支 →
  `FUN_00597A10 @ 0x597A10`（仅 0x1D 字节的跳板函数）→ **真身是某个对象的
  vtable+4 虚函数**（下轮第一步：从调用上下文定位该对象类型）
- **随机数发生器已完整还原**（"逻辑对齐"的基石）：
  - `Randomizer::Random @ 0x65C780`：R250 算法——250 个 uint 状态 + 双索引
    XOR 回绕（结构：+0xC 起 250×4B 状态，+4/+8 两个索引，0xF9 回绕）
  - `Randomizer::RandomRanged @ 0x65C7E0`：区间随机 + 拒绝采样（去模偏差）
  - Phobos hook `Random2Class_Random_SyncLog` 证实：联机各端随机序列同步
    → **同一种子 = 同一条随机序列 = 同一张地图**（这就是"逻辑对齐"的验证手段）
- **读图入口已确认**（命令行程序的输出端对齐目标）：
  - `ReadMap = FUN_00689E90 @ 0x689E90`：读 `[Header]`（StartX/StartY/Width/
    Height/NumberStartingPoints/NumCoopHumanStartSpots）、`[Waypoint]`
    （Read2Integers）、`[Basic]`（NextScenario/Intro/Brief 等）
  - `.map` 是 INI 风格文本格式，最终生成的 .map 必须能被 ReadMap 完整读取
- **取证文件**：`memory/data/decomp/randmap_probe.txt`
- **分析脚本**：`code/ghidra_scripts/decompile_randmap.py`

### 下一步计划（按顺序）

1. 定位跳板 `FUN_00597A10` 的调用上下文（在 0x684620 的随机分支），确认
   vtable+4 虚函数对应的对象类型 → 反编译**地图生成器本体**
2. 还原生成流程：地图尺寸/地形高度生成 → 矿点放置 → 玩家起始点 → 物件/装饰放置
   （留意 Randomizer 的调用顺序与参数——顺序即种子兼容性）
3. 对照 `ReadMap`（0x689E90）整理 `.map` 输出格式（[Header]/[Waypoint]/[Basic]/
   地形格/Cells 等节）
4. 在 `code/rewrite/` 用 C++ 独立重写生成算法 + 数值测试
5. 验证：同一种子输入 → 游戏内加载的地图与原版随机生成结果一致

**注意**：RA1 源码（`D:\CnCRemastered\SOURCECODE`）**没有**随机地图生成器
（这是 TS/RA2 引入的特性），不能照搬 RA1 实现；但其底层工具（随机数、地形、
`RadarClass`、`CellClass` 等）可作理解辅助。最终结论一律以 YR 反编译 +
汇编核对为准。

## 标准工作流（与 QWEN.md 对齐）

```
符号对号入座 → 反编译 → 汇编核对 → 常量取证 → 规则映射 → 算法重写 → 数值测试
```

1. **符号对号入座**：YRpp 符号表（`memory/data/symbols/`）已在 Ghidra 工程中应用
2. **反编译**：`code/ghidra_scripts/decompile_*.py` 模板（注意 Jython 2.7 陷阱：
   `memory/notes/ghidra-jython-pitfalls.md`）；大函数反编译超时就把 timeout 调大
   （WinMain 0x6BB9A0 用了 300 秒）
3. **汇编核对**：反编译对浮点/条件逻辑可能失真，模糊点 dump 汇编逐条还原
4. **常量取证**：`code/read_constants.py` 从 PE 读 `.rdata` 原始字节
   （注意 float/double 区分）
5. **算法重写**：数据驱动（rules 数值不硬编码）；测试程序可硬编码原版默认值
6. **数值测试**：Google Test，CI 自动跑

## 硬性规范

1. **数据驱动**：依赖 rules.ini 的数值通过 BuildRules 注入，不硬编码
2. **注释克制**：只写地址依据和行为依据，不写废话注释
3. **取证优先**：断言地址/常量语义前，必须用反编译/汇编/PE 字节三重验证之一；
   无法验证的标注"未确认"——绝不编造证据
4. **半偶舍入**：原版 x87 FRNDINT（ties-to-even），重写用 `std::nearbyint`
5. **诚实报告**：测试失败如实报告；没跑过的验证步骤说没跑过

## 环境速查

- Ghidra headless：`D:\ghidra_11.1_PUBLIC_20240607\ghidra_11.1_PUBLIC\support\analyzeHeadless.bat`
- Ghidra 工程：`E:\code\ra2-reverse\ghidra_proj\RA2`（gamemd.exe 全量分析，三层符号已应用）
- 运行示例：
  `analyzeHeadless.bat E:\code\ra2-reverse\ghidra_proj RA2 -process gamemd.exe -noanalysis -postScript <脚本> -scriptPath E:\code\ra2-reverse-AI\code\ghidra_scripts`
- 原版二进制：`E:\YRLauncher\gamemd.exe`（只读分析，永不修改）
- 交叉验证源码：YRpp `E:\code\YRpp`、Phobos `E:\code\Phobos`、RA1 `D:\CnCRemastered\SOURCECODE`
- 反编译输出惯例：脚本自动输出到 `memory/data/decomp/`（基于脚本位置动态定位仓库根，无硬编码路径，无需手动拷贝）

## 协作约定

- 本仓库由 **Claude Code 与 Qwen Code 协作维护**，两边工作手册（本文件与
  `QWEN.md`）内容对齐；产出都要落盘到仓库（文档/脚本/取证），可被对方接力
- 每轮交付必须带证据（地址 + 验证方式），不空口断言
- 发现与社区已知行为矛盾时，以实测为准并记录差异
