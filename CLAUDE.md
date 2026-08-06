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
| `D:
a2mdpost\MapGen.cpp`（字符串 @0x82BA48） | **0x5956E7 – 0x59A63A** | 3934 – 6746 |

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

#### 4.5 浮点环境与取数惯用法（2026-08-06 傍晚新增，**复刻前提**）
- **全局 FPU 状态实锤**：WinMain `0x6BBFB7 push 0x300 / push 0x300 / call 0x7CBF49`
  = `_control87(_RC_CHOP, _MCW_RC)` → **舍入模式 = 向零截断**；
  紧接 `0x6BBFC9 call 0x7C5EE4` 把控制字缓存进 `[0x822D80]`，实测 **0x0E7F**
  （PC=53 位双精度 + RC=chop + 全异常屏蔽）；`_ftol @0x7C5F00` 的快路径正是
  比对这个缓存值——三条证据互证
- **生成器全局 Randomizer @ 0xABE890**：`FUN_00598960` 开头用 `MapSeedClass+0x74`
  （即 `.sed` 的 `Seed=`）在栈上构造后 `rep movsd 0xFD` 拷进全局
  → **整张地图只播种一次**，所有阶段共用一条随机序列
- **取数惯用法不是 RandomRanged**：按 `this == 0xABE890` 配对后，生成器共
  **150 处 `Random`、0 处 `RandomRanged`**（`code/randmap/scan_rmg_rng.py`）。
  按函数地址数出的"14 处 Ranged"用的都是引擎自己那个 Randomizer 对象——
  **判断"谁在取随机数"必须看 `this`，不能只看调用了哪个函数**。
  再往上追引用：**46 处落在零引用的死代码里**（22 个函数，`0x5A5020` 那棵子树
  一整棵都是），真正会执行的是 **104 处**，已逐点归到 12 个生成阶段名下
  （`code/randmap/call_closure.py` → `docs/random-map/rng-ledger.md`）。
  归属要用**指令级递归下降**而非调用图：Ghidra 在 MapGen 这段有大量未定义函数，
  图会断；switch 跳转表要解；函数指针边要人工补
  （`UniformUnit@0x598000` 只经 `NormalDeviate` 的 `call [esi+0x10]` 被调用）。
  算法主体用的是浮点比例 + 拒绝采样：
  `do { v = (int)((double)(uint32)Random() * (n/4294967295.0)); } while (v > n-1);`
  比例常数已核对（`0x7ED9E8`=10 / `0x7ED9E0`=242 / `0x7ED9D8`=201 各除以 2^32-1，
  与 `n/4294967295.0` 的 double 字节逐位一致）；`fild qword` 是故意的
  ——x87 没有 uint32 转换指令，写成 int32 会让一半取值变负
- 重写 `code/rewrite/rmg/scaled_random.{h,cpp}`（含 `FpuChopScope` RAII）+
  Google Test 3 项全过；基准 `memory/data/rmg/scaled_golden.txt`
- **诚实记录**：6 个 n × 200 种子 × 2000 抽（各 40 万次）对比 chop 与 round-to-nearest
  **分歧 0 次**（乘积落在整数边界半 ulp 内的概率约 n·2^-53）。舍入模式对"取随机数"
  无风险，但它是**全局**设置，做真正浮点累加的地形阶段仍必须在同一模式下跑
- 顺带实锤：`0x87F7E8 MapClass::Instance`、`0x89C2DC` 工作数组边长（方阵）、
  `0xABED10` 每格 0x50 字节工作记录数组（`0x578E60` 分配 `side²×0x50`），
  索引 `(Y*side + X)*0x50`；记录字段语义**未确认**
- 文档：`docs/random-map/fpu-and-random-idioms.md`；取证 `memory/data/rmg/rng_callsites.txt`

#### 4.6 原版数学基元与涂块核心（2026-08-06 夜，**形状的全部来源**）
- **`FastSqrt @0x4CAC40` 不是真开方**：拆 float 位形，用尾数高 14 位查 `0x8650BC` 处
  16384 项表，误差实测 5.7e-5 = 2⁻¹⁴。与 `std::sqrt` 系统性不等，**不可替换**。
  表可由**纯整数**重算：`table[i] = isqrt(m>=2²³ ? 2m·2²³ : (2²³+m)·2²³) - 2²³`
  ——16384/16384 与 exe 逐位相同，因此仓库里不留任何提取的二进制资产
- **`NormalDeviate @0x5980C0`**：Marsaglia 极坐标法正态分布，对象在 `0xABDFB8`，
  由 `FUN_00598960` 与全局 Randomizer 一并初始化（`+0x10` = 均匀源 `0x598000`）。
  一次算两个、**缓存第二个** —— 调用次数的奇偶性直接改变随机数流节奏，不可省略；
  拒绝（落在单位圆外）会额外消耗 2 个 `Random()`
- **`fyl2x` 语义——本模块唯一一次结论被推翻，教训优先记住**：
  - 曾用 Unicorn 取基准、80 样本对拍得出"`fyl2x` ≡ 精确 ln 向零截断"。**这个证据是坏的**：
    样本量提到 20000 后发现分歧，进一步发现 **Unicorn 的 `fyl2x` 完全不理会 FPU 控制字**
    （PC/RC 四种组合返回同一值），它算的是宿主 long double。**模拟器在超越指令上不可信**
  - 写真 CPU 只读探针取真值（`build/scratch/fyl2x_probe.cpp`，实测 FPCW `0x027F`→`0x0E7F`）：
    真 `fyl2x` 与 `chop53(精确 ln)` 分歧 **7/20000 = 0.035%**，一律**低 1 ulp**，
    全部落在精确值距截断边界不足 0.4 个 80 位 ulp 处 —— **`fyl2x` 本身就不是正确舍入的**
  - **因此产品代码不用本机 `fyl2x`**（跟着它走 = 把输出绑死在 CPU 型号上）。
    `ChopLog` 定义为唯一确定的数学值「精确 ln 向零截断」，用 `dd_log.h` 双双精度实现，
    **所有架构同一条路**，与任意精度模型 20000/20000 逐位一致
  - 残余 0.035% 的 1 ulp **不传下去**：唯一消费者 `FastSqrt` 只取尾数高 14 位
    （量化 2⁻¹⁴），2⁻⁵² 扰动翻转它的概率约 2⁻³⁸，实测穿透 0 次。
    旁证：Unicorn(≈near53) / 真 CPU(fyl2x) / 复刻(chop53) **三种不同的 ln，
    过了 `FastSqrt` 之后产出同一批位形**
  - `build32/` 的定位随之改变：不再是"为了用同一条指令"，而是**取证平台**
  - **方法论**：①"逐位一致"的样本量必须与目标分歧率匹配，80 个样本发现 0.035% 分歧的
    概率只有 3%；② **oracle 本身要验证**——改一个本该改变结果的输入，若结果纹丝不动，
    说明它没在模拟那个语义；③ 指令本身不是正确舍入时，"复刻它"是伪目标，
    应定义确定语义再**证明**残余差异被下游吸收
- **涂块核心 `FUN_0059BBC0` 已完整还原**：Dijkstra 式最小堆扩散。每次弹出优先级最小的
  候选格变成陆地，把 4 个基本方向邻格入堆；优先级 = `FastSqrt(到引力中心距离)` +
  均匀 `[0,5)` 抖动，而**引力中心每步做布朗运动**（`Normal() × 步长`，MapType 0 用 0.25、
  MapType 1 用 0.75）——不规则海岸线全部源于此。区域约束是矩形的内切椭圆（`0x59BAB0`）。
  收尾跑 `0x57A0C0` 铺岸线，**失败则整块抹回海里**（这就是"大片是海"的机制）
- 邻接表 `0x89F688`（.bss，初始化于 `0x49F2F0`）：8 项 `{int16 dx,dy}` =
  `(0,-1)(1,-1)(1,0)(1,1)(0,1)(-1,1)(-1,0)(-1,-1)`，涂块只取偶数下标
- **可移植性加固**（应"CI 可能跑在 x86/ARM/龙芯"的要求）：sqrt 表**写死**在
  `sqrt_table.h` 让架构无关性在编译期成立（并用纯整数 `isqrt` 重算全表自检）；
  `fp_guard.h` 用 `static_assert` 挡 x87 超精度与有符号右移歧义，
  用编译选项挡 FMA 收缩与 `-ffast-math`，再用运行期自检确认 `fesetround` 真的生效
- 产出：`code/rewrite/rmg/{fast_math,dd_log,sqrt_table,fp_guard}.*` + `test_fast_math.cpp`
  （10 项全过：74 组 sqrt + 16384 项表自检 + 101 组架构无关 ChopLog 黄金值 +
  5 种子 × 16 正态，**全部逐位比较**）；基准 `memory/data/rmg/{fastmath_golden,
  fyl2x_native,fyl2x_mismatch,choplog_golden}.txt`
- 文档：`docs/random-map/fast-math.md`、`docs/random-map/stage-seeding-water.md` §三

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

1. **逐阶段还原算法**。已确认 `Init random map`（`FUN_00599650`）主体是**引擎侧初始化**
   （写 `[Lighting]`/`[Basic]` INI、建 Preview、清 CellClass、重算寻路、设 waypoint 700/699），
   只取 1 次随机数——命令行工具基本不需要复刻它。真正的地形算法在
   `Seeding water`（`0x59A6C0` / `0x59C580` / `0x59C630`）及之后，
   且集中在 **0x59A66C-0x5A9800 这段无调试宏的源文件**里（约 130 处取数点）。
   `Seeding water` 的涂块核心与数学基元已完成（见 4.6），**剩余缺口**：
   投放策略 `FUN_0059A8F0`（疑似按玩家数分区的 `(x,y,w,h)` 四元组）、
   岸线平滑 `FUN_0057A0C0`（决定一块陆地被接受还是抹回海里）、
   各 MapType 的取数个数统计、工作记录 0x50 字节的完整字段布局
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
4. **舍入方式逐点确认，不许套默认**：x87 `FRNDINT` 走的是 ties-to-even（重写用
   `std::nearbyint`），但原版大量地方用的是 `_ftol`（向零截断）甚至
   `trunc(x ± 0.5)`（round-half-away-from-zero，见
   [stage-hills.md](docs/random-map/stage-hills.md) §2.5）。
   **每个取整点都要看汇编再决定用哪个**
5. **诚实报告**：测试失败如实报告；没跑过的验证步骤说没跑过
6. **连 bug 一起复刻**（2026-08-06 定）：目标是逐字节一致，不是"写一个更好的
   地图生成器"。原版的溢出、截断、逻辑漏洞**全部照抄**，并在文档里显式标注
   「原版怪癖，照抄」——见下方清单。任何"顺手修一下"都会让对拍失败

### 原版怪癖清单（已实锤，必须照抄）

西木的注释写得像 prompt，实现却相当粗糙。以下是目前已取证的、
**看起来像 bug 但必须一字不改地复刻**的地方：

| 位置 | 怪癖 |
|------|------|
| `RandomRanged@0x65C7E0` | 求位数只扫到第 30 位；`bits==32` 时掩码退化成 0（x86 `SHL` 只取 `CL` 低 5 位）；`range+1` 溢出时直接返回不采样 |
| `PickBelow` 惯用法 | `fild qword` 把 32 位随机数当**无符号**读，但上界 `n` 也走同一条路——`n` 为负时行为诡异 |
| `PickRange@0x598030` | `+lo` 发生在 `_ftol` **之前**，且拒绝判定用**无符号** `ja` 与 `hi` 比 |
| 阶段 6 出生点 | 外层重试循环：失败一轮**已经花掉的随机数不退回** |
| 阶段 7 科技建筑 | `work[idx].xy == (0,0)` 被拒时消耗随机数却**不递增 `tries`**；`tries > 200` 强行用格子 `(0,0)`；`n==0` / `m==0` 也照样烧一个随机数 |
| 阶段 8 布矿 §2.1 | 出生点重心用 **16 位** `bx`/`bp` 累加，坐标大时**溢出回绕**；`best` 每轮被截断成 int 再参与下一轮浮点比较 |
| 阶段 9 造山 §2.5 | 高度取整是 `trunc(h ± 0.5)`，**不是** ties-to-even |
| 阶段 9 造山 §2.4 | 两处截断正态里 `[esp+0x10]` / `[esp+0x40]` 谁是 `lo` 谁是 `hi` **是反过来的** |
| 阶段 8 布矿 §三 / 阶段 10 §四 | 两处 `sprintf` 选资源名都用 `PickRange(1,n)` 而不是 `PickBelow(n)`——**`TIBTRE00` 与 `TREE00` 永远不会被生成**，美术资源白做了两个 |
| 阶段 8 布矿 §3.1 | 每次"冷启动"都把**全图** `Work[].+0x3C` 清一遍，一片矿田最坏清 10 次；且"本次爆发第一格"落定时会把已入队的邻居**连池带堆全作废** |
| 阶段 10 LAT §一 | `0x5A38C0` 岸格的 5×5 邻域循环，写目标指针在进循环前就按**中心格**算死了（`0x5A3909-0x5A3920`），循环变量只参与边界判定——本该扩散一片，实际对中心格**重复写 25 次同一个值** |
| 阶段 10 LAT §2.6 | 撒石头时既非沙 LAT、也非平地/绿地的格子，**不写 overlay 却照样 `placed++`**，白吃一个名额还多跑一次 `RecalcAttributes` |
| `NormalDeviate@0x5980C0` | 缓存标志**跨阶段串**——同一次抽样花掉的随机数个数不是定值 |

> 这些东西 2001 年发出去之后就没人动过。**让 25 年前的代码原样重见天日**
> 才是这个项目的意义，"修 bug"是另一层目标（重写红警 2）该干的事。

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
