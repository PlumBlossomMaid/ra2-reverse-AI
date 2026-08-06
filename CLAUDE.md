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

**不是**：分发游戏、包含原版代码/素材、绕过任何版权保护、复制原版实现。
（"重写"= 独立实现 + 行为对齐，非复制代码——见下方愿景与 OpenRA 先例）

**宏远愿景（2026-08-06，用户定调）**：三层目标——
1. **完美复刻**：随机地图生成器不是"新写一套能用就行"的工具，而是**字节级复现
   原版逻辑**（同一种子 → 同一张 .map，哈希对比验证）。生成地图只是副产品
2. **为重写铺路**：为"完美重写红警2"（引擎级 reimplementation）积累子系统拼图。
   本模块是第一个交付条件成熟的基石：输入输出清晰（种子+参数→.map）、验证硬核
   （游戏内加载对比 + 字节哈希）、边界干净（不依赖运行时其他系统）
3. **回馈社区**：开源填补空白——社区有 XCC（.map 读写）、Ares/Phobos（mod 扩展），
   但**字节级复现原版随机地图生成逻辑的独立工具是空白**；路线参照 OpenRA
   （黑盒行为对齐 + 独立实现，RTS 重写标杆）

> **对齐标准提醒**：原版生成器允许"不平衡/偶尔离谱"的地图（孤岛开局、半张废图、
> 小图速推）是**原版口味**——复刻出孤岛是**对齐成功的证据**，不是 bug；反之
> 原版出孤岛而我们出不了，说明约束规则抄漏了（对齐失败）。

**社区先例（同领域的公开、长期存在的实践）：**
- **YRpp**（GPL 开源）：社区 20 年维护的 YR 类库与符号表——本项目的地址信息
  全部来自这份公开资料，仓库内 `memory/data/symbols/` 只存引用不存代码
- **Ares / Phobos**（GPL 开源）：YR 扩展项目，本仓库用其 hook 地址表做交叉验证
- **EA 官方开源 C&C 源码**（2020 年，GPL v3）：EA 主动开源了红警 1/C&C 的源码，
  本仓库 `docs/references/` 记录了 YR 与 RA1 的对照关系（RA2 引擎是 RA1 的演化）
- **EA 官方任务编辑器**（2025 年开源，GPL-3.0）：`E:\code\CNC_TS_and_RA2_Mission_Editor`，
  .map 读写的权威实现（无随机地图生成器）

**严格遵守的法律边界：**
1. 只读分析二进制，绝不修改、重打包、分发 `gamemd.exe` 本体
2. 仓库不含任何受版权保护的游戏代码/美术/音频素材
3. 地址与符号信息仅来自社区公开资料（YRpp GPL）
4. 算法重写为独立实现，不复制原版汇编/伪代码进产出物

## 下一步任务：遭遇战随机地图生成器逆向

**目标**：写一个命令行程序——输入参数（随机种子 / 地图尺寸 / 玩家数 / 地形主题），
输出一个与**原版逻辑对齐**的 YR `.map` 文件（可直接被游戏加载，生成的局势与原版
随机地图一致）。这是一个独立实现的算法程序，是上述研究文档化的自然延伸。

### 当前进度（2026-08-06 下午，RNG 里程碑已达成）

**结论先行**：随机数发生器（R250）已 **逐位复刻并通过对拍**；`MapGen.cpp` 整个编译单元
已测绘完毕；生成流水线的 11 个阶段及其入口函数全部锁定。下一步是逐阶段还原算法。

#### 1. 编译单元测绘（方法论升级）
MSVC 6.0 调试宏 `PUSH <行号 imm32>; PUSH <文件名VA>; CALL 0x734E60` 在 PE 里留下
`68 <行号> 68 <文件VA>` 字节对——直接扫 PE 就能把源文件反推成代码地址区间，
**不用开 Ghidra**。工具：`code/randmap/scan_source_macros.py`。

| 源文件 | 代码区 | 源码行范围 |
|--------|--------|-----------|
| `D:a2mdpost\MapGen.cpp`（字符串 @0x82BA48） | **0x5956E7 – 0x59A63A** | 3934 – 6746 |

顺带产出全 exe **33 个源文件 ↔ 代码区**映射，后续任何模块都能直接查表定位。

#### 2. 生成器类实锤：`MapSeedClass`
- YRpp 符号交叉验证：`MapSeedClass : LoadOptionsClass`，单例 `Instance @ 0xABDFD8`，
  对象大小 `0x318`，构造函数 `MapSeedClass::MapSeedClass @ 0x595680`
- 本仓库先前独立还原的 **17 参数偏移表与 YRpp `MapSeedClass.h` 完全吻合**（双向验证通过）
- **修正先前误标**：`0x597760` 是 `MapSeedClass::SaveMission`（不是 WriteParameters），
  `0x597A30` 是 `MapSeedClass::LoadMission`（不是 ReadParameters）——它们内部确实做
  参数读写，但语义是存/读任务档。详见 `docs/random-map/parameters.md`
- **修正先前假设**：`RandMap.img` 是 **预览图**（`PreviewClass` / `RawFileClass` 读写），
  不是地形模板——"未解的地形值流"假设作废

#### 3. 生成流水线全景（主流程 `FUN_00598960`，3018B）
原版自带的调试日志字符串等于交出了算法目录：

| 阶段日志 | 入口函数 |
|----------|----------|
| `RMG: Init random map` | `FUN_00599650`（4124B） |
| `RMG: Seeding water` | `FUN_0059A6C0` / `FUN_0059C580` / `FUN_0059C630` |
| `RMG: Init regions` | `FUN_005AC290` / `FUN_0058CF90` … |
| `RMG: Making regions` | `FUN_0058EBC0` / `FUN_0058EF10` / `FUN_005A19E0` / `FUN_005A17F0` |
| `RMG: Recalculating cell attributes` | `FUN_0059B740` + `CellClass::RecalcAttributes(0x47D2B0)`（共出现 4 次） |
| `RMG: Creating starting points` | `FUN_00594B50` / `FUN_005A1FB0` |
| `RMG: Adding tech buildings` | `FUN_005A95B0` |
| `RMG: Creating tiberium` | `FUN_005A23A0` |
| `RMG: Creating hills` | `FUN_005A35F0` |
| `RMG: Creating LATs, rocks etc` | `FUN_005A38C0` / `FUN_005A3AE0` / `FUN_005A4280` |
| `RMG: Cleanup` → `RMG: Compute Radar Image` → `RMG: Done` | `FUN_00722D00` / `RadarClass::SetVisibleRect(0x654490)` |

**阶段顺序即种子兼容性**：每阶段消耗的随机数个数必须与原版一致，少取一次后面全部错位。

#### 4. 随机数发生器 R250 —— 已逐位复刻 ✅
| 原版地址 | 语义 | 复刻位置 |
|----------|------|----------|
| `0x65C6D0`（RET 4） | 播种：4 轮 Feistel 混合，常量表 `0x839644` / `0x839690` 各 4 个 uint | `Randomizer::Seed` |
| `0x65C780` | 取数：R250（滞后 103/250，XOR 移位寄存器） | `Randomizer::Random` |
| `0x65C7E0`（RET 8） | 区间随机：掩码 + 拒绝采样（去模偏差） | `Randomizer::RandomRanged` |

- **汇编级坑点**（伪代码会骗人，必须核对汇编 `memory/data/decomp/randomizer_asm.txt`）：
  `SAR EDX,0x10` 是**算术**右移，随后 `SHL/OR` 拼半字 —— **不是 ROR16**，符号位会污染高半字。
  照伪代码写会在约一半种子上分岔
- 产出：`code/rewrite/rmg/randomizer.{h,cpp}` + `test/test_randomizer.cpp`（Google Test 7 项全过）
- 基准：`memory/data/rmg/rng_golden.txt`（8 个种子）

#### 5. 验证方法论：Unicorn 指令级 oracle（可复用于后续每个阶段）
`code/randmap/emu_oracle.py` 把 gamemd.exe 的节按原 ImageBase 0x400000 映射进
Unicorn，用哨兵返回地址 + ECX 传 this 直接跑原版机器码取标准答案，再与独立重写对拍。

- 为什么不用进程内调用：64 位 Windows 上宿主进程堆在加载器阶段就铺满 0x400000-0x6C0000，
  `VirtualAlloc(0x400000)` 必返 ERROR_INVALID_ADDRESS(487)，`/BASE` `/FIXED` `/ENTRY` 抢跑都来不及
- 附带好处：**纯只读文件分析**，不加载/不修改/不分发原版二进制，法律边界更干净
- 对拍结果：5 个种子 × (播种状态首尾 + 16 次 Random + 3 组 16 次 RandomRanged) **逐位完全一致**

#### 6. 文件系统契约（早前成果，仍有效）
- `RandMap.Sed`（264B）：`[RandomMap]` INI，生成器输入参数全集（Width/Height/NumPlayers/
  Seed/MapType/Theater/Time/RegionSize/Ruggedness/Accessibility/WaterAmount/Tiberium/
  TiberiumLayout/Vegetation/UrbanPresence/Resources/Description）
  ——`Description` 是逗号分隔的 UTF-16 码元十六进制
- 输出落地 `rmcache\RandMap.Map`；`*.mmp` 是参数档过滤器
- `.map` 权威格式（EA 官方编辑器源码）：`MAPFIELDDATA` 11B/cell；
  `[IsoMapPack5]` = Base64 → 块(2B srcSize + 2B destSize + 数据) → XCC `decode5s`

### 下一步计划（按顺序）

1. **逐阶段还原算法**，从 `RMG: Init random map`（`FUN_00599650`）开始，
   每个阶段都用 Unicorn oracle 对拍，**同时记录该阶段消耗的随机数个数**
2. `.sed` 参数解析（含 UTF-16-hex `Description`）+ rules.ini 驱动的 RMG 配置（数据驱动）
3. `.map` 输出：`[IsoMapPack5]` Base64 + XCC encode5，组装成 C++ 命令行程序
4. 端到端验证：同种子 + 同参数 → 我们的 `.map` 与原版输出**逐字节比对**

**注意**：RA1 源码（`D:\CnCRemastered\SOURCECODE`）**没有**随机地图生成器
（这是 TS/RA2 引入的特性），不能照搬 RA1 实现；但其底层工具（随机数、地形、
`RadarClass`、`CellClass` 等）可作理解辅助。最终结论一律以 YR 反编译 +
汇编核对为准。

### 相关文档与取证

- 文档：`docs/random-map/README.md`（总览与方法论）/ `randomizer.md`（RNG）/
  `mapgen-tu.md`（编译单元与流水线）/ `parameters.md`（参数与 .sed 格式）
- 取证：`memory/data/decomp/mapgen_tu_map.txt` / `rmg_pipeline_decomp.txt` /
  `randomizer_asm.txt`；基准 `memory/data/rmg/rng_golden.txt`
- 环境坑记录：`memory/notes/toolchain-pitfalls.md`（7 条，含 shell 转义/GBK 终端/
  Ghidra scriptPath/Python tempdir 等，踩过一次别再踩）

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
- 交叉验证源码：YRpp `E:\code\YRpp`、Phobos `E:\code\Phobos`、
  EA 官方任务编辑器 `E:\code\CNC_TS_and_RA2_Mission_Editor`（GPL-3.0，.map 读写权威实现）、
  RA1 `D:\CnCRemastered\SOURCECODE`
- 反编译输出惯例：脚本自动输出到 `memory/data/decomp/`（基于脚本位置动态定位仓库根，无硬编码路径，无需手动拷贝）

## 协作约定

- 本仓库由 **Claude Code 与 Qwen Code 协作维护**，两边工作手册（本文件与
  `QWEN.md`）内容对齐；产出都要落盘到仓库（文档/脚本/取证），可被对方接力
- 每轮交付必须带证据（地址 + 验证方式），不空口断言
- 发现与社区已知行为矛盾时，以实测为准并记录差异
