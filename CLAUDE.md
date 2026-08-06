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
- **文件系统契约已实锤**（2026-08-06 晨，exe 字符串 + 游戏目录实物）：
  - `RandMap.Sed`（游戏目录实物，264B）：`[RandomMap]` INI，**生成器输入参数全集**——
    Width / Height / NumPlayers / Seed / MapType / Theater / Time / RegionSize /
    Ruggedness / Accessibility / WaterAmount / Tiberium / TiberiumLayout /
    Vegetation / UrbanPresence / Resources / Description（实测值：Width=3 Height=3
    NumPlayers=8 Seed=11584 MapType=3 Theater=0 RegionSize=11 Ruggedness=59 ...）
  - exe 引用：`RandMap.img` @ 0x429abc、`rmcache\RandMap.Map` + `rmcache` @ 0x42bb44、
    `RandMap.Sed` + `lastmap.sed` + `GUI:LoadMapMenu/GUI:SaveMap` @ 0x42bc30、
    `*.mmp`（随机地图参数文件过滤）+ `rmcache\` @ 0x42bb24、`".SED"` +
    `"Scen->IsRandom = true"` 日志 @ 0x43da5e
  - `RandMap.img`（游戏目录实物，51,859B 二进制）：头 `0a 05 01 08` + (0xC5,0x63)
    (0xC6,0x64) 坐标对，0x88 起密集地形值流（0xC0-0xF0 为主，疑似高度/地形编码）——**结构未解**
  - **推断**：生成流程 = 读 `.sed`/`.mmp` 参数 → R250 生成 → 写 `rmcache\RandMap.Map`
    → **找到读 `.sed`/`.mmp` 的函数 ≈ 生成器入口**（比从 vtable 跳板绕更直接）
- **取证文件**：`memory/data/decomp/randmap_probe.txt`
- **分析脚本**：`code/ghidra_scripts/decompile_randmap.py`

### 生成器参数读写已定位（2026-08-06 上午，Ghidra xref + 汇编逐条还原）

**关键背景**：随机地图相关字符串在 .data 里连续存储（0x829abc-0x82bcff 区块：
`RandMap.img` / `RandomMap` / `*.mmp` / `RandMap.Sed` / `lastmap.sed` 等），
xref 查到的引用函数全部集中在 **0x597xxx 区 = MapGeneratorClass 方法区**
（源码文件名从调试宏实锤：`D:\ra2mdpost\MapGen.cpp` @ 0x82ba48）。

**函数清单**（全在 0x597000-0x598000，`memory/data/decomp/randmap_gen_asm.txt` 1171 条指令）：
| 地址 | 判定 | 证据 |
|------|------|------|
| 0x597757（RET 0x8） | WriteParameters 保存参数 | 16×CCINIClass::WriteInteger(0x5275c0)+1×WriteString(0x528e00) |
| 0x597a30（RET 0x4） | ReadParameters 读参数 | 16×CCINIClass::ReadInteger(0x5276d0)+1×ReadString(0x528f00)，节名"RandomMap"@0x82bb24 |
| 0x597d60（RET 0x8） | 加载函数 | strcmp"RandMap.Sed"/"lastmap.sed"→0x7c8d20，文件名拷贝 [this+0x100] 32B，[p2+0x14/0x18]→[this+0x1ac/0x1b0] |
| FUN_00597a10 | 虚函数跳板 | `obj->vtable[1](arg)`，null 时调 0x5587f0 |
| 0x597f80-0x597ff6 | 4 个日志包装 | 调试宏（GUI:LoadMapMenu/SaveMapMenu/DeleteMapMenu/MapSaved） |

**17 参数 ↔ 对象偏移映射（[RandomMap] 节，键名已还原）**：
Description(+0x78, 字符串) / Width(+0x64) / Height(+0x68) / NumPlayers(+0x50) /
Seed(+0x74) / MapType(+0x3c) / Theater(+0x38) / Time(+0x48) / RegionSize(+0x70) /
Ruggedness(+0x44) / Accessibility(+0x6c) / WaterAmount(+0x4c) / Tiberium(+0x54) /
TiberiumLayout(+0x58) / Vegetation(+0x5c) / UrbanPresence(+0x60) / Resources(+0x40)

**调试宏格式**：`PUSH 0x82ba48("D:\ra2mdpost\MapGen.cpp") + PUSH 行号 + CALL 0x734e60`
→ 反编译里能反推源码行号。取证：`randmap_gen_asm.txt` / `randmap_xref.txt` /
`_dump_randmap_strs.py`（键名还原）。

### .map 二进制格式（输出端，EA 官方编辑器源码实锤）

**EA 官方开源 `CNC_TS_and_RA2_Mission_Editor`**（FinalSun/FinalAlert2，GPL-3.0，
已 clone 到 `E:\code\CNC_TS_and_RA2_Mission_Editor`，1.39 MiB）——没有随机地图
生成器，但是 .map 读写的**权威实现**：

- `MAPFIELDDATA`（11 字节/cell，MapData.h）：u16 wX + u16 wY + u16 wGround(tile)
  + bData[3] + bHeight + bData2[1]
- `[IsoMapPack5]` 节（MapData.cpp） = Base64 → 块序列（2B wSrcSize + 2B wDestSize
  + 压缩数据）→ decode5s(XCC 压缩) → MAPFIELDDATA[]，总长 / 11 = cell 数
- `[OverlayPack]`/`[OverlayDataPack]` = Base64(encode80 压缩)，`[Digest]` = 10×u16 随机盐
- XCC 压缩算法源码：`3rdParty/xcc/misc/shp_decode.cpp`（encode80/decode5/decode5s/encode5）
- **注意**：游戏端 ReadMap 0x689E90 只读 [Header]/[Waypoint]/[Basic]；
  **IsoMapPack5 解压在游戏里的位置待定位**（下午对照 decode5s 验证）

### 下一步计划（按顺序）

1. **追生成主流程**（参数读写已定位，下一步找 Generate）：
   - 反编译 FUN_00596e50（0x596E50 起长函数，覆盖 0x5970xx）+ 0x597260-0x597757
     的 5 个小函数，找 RandMap.img 读取与地形生成
   - 从 WriteParameters 调用者（写 .sed/.mmp 的上层）反推调用链
2. **RandMap.img 结构**：从读取它的函数反编译字段布局（对照字节探针
   `_probe_randmap_img2.py`：头部 magic `0a 05 01 08` + 坐标对 (197,99)(198,100)
   + 0x40 处 768/198/1/198/100；数据流 0x88 起 51723B 无整齐整除宽度）
3. **游戏端 IsoMapPack5 解压定位**：对照 EA 源码 decode5s 验证
4. 还原生成流程：地形高度生成 → 矿点放置 → 玩家起始点 → 物件/装饰放置
   （留意 Randomizer 0x65C780 调用顺序——顺序即种子兼容性）
5. 在 `code/rewrite/` 用 C++ 独立重写生成算法 + 数值测试
6. 验证：同一种子输入 → 游戏内加载的地图与原版随机生成结果一致

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
- 交叉验证源码：YRpp `E:\code\YRpp`、Phobos `E:\code\Phobos`、
  EA 官方任务编辑器 `E:\code\CNC_TS_and_RA2_Mission_Editor`（GPL-3.0，.map 读写权威实现）、
  RA1 `D:\CnCRemastered\SOURCECODE`
- 反编译输出惯例：脚本自动输出到 `memory/data/decomp/`（基于脚本位置动态定位仓库根，无硬编码路径，无需手动拷贝）

## 协作约定

- 本仓库由 **Claude Code 与 Qwen Code 协作维护**，两边工作手册（本文件与
  `QWEN.md`）内容对齐；产出都要落盘到仓库（文档/脚本/取证），可被对方接力
- 每轮交付必须带证据（地址 + 验证方式），不空口断言
- 发现与社区已知行为矛盾时，以实测为准并记录差异
