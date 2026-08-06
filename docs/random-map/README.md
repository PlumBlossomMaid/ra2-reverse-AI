# 遭遇战随机地图生成器（RMG）逆向

目标：**字节级复刻**原版 `gamemd.exe` 的随机地图生成算法——同一组参数 + 同一种子
必须产出与原版完全相同的地图。生成地图只是副产品，真正的产出是"可验证的对齐"。

## 文档索引

| 文档 | 内容 | 状态 |
|---|---|---|
| [mapgen-tu.md](mapgen-tu.md) | MapGen.cpp 编译单元测绘 + 生成流水线全景 | ✅ 骨架完成 |
| [randomizer.md](randomizer.md) | 随机数发生器 R250（播种 + 取数 + 区间随机） | ✅ 逐位对齐 |
| [parameters.md](parameters.md) | MapSeedClass 参数集与 `.sed` 文件格式 | ✅ 已实锤 |
| [fpu-and-random-idioms.md](fpu-and-random-idioms.md) | 原版全局浮点环境 + 生成器取随机数的惯用法 | ✅ 已实锤并对拍 |
| [fast-math.md](fast-math.md) | 查表近似 sqrt + Marsaglia 正态分布（形状的数学来源） | ✅ 已实锤并对拍 |
| [rng-ledger.md](rng-ledger.md) | **随机数消耗总账**：104 个取数点 + 阶段归属 + 46 处死代码 | ✅ 清单已实锤 |
| [stage-init.md](stage-init.md) | 阶段：Init random map（搭空 scenario，只抽 1 个随机数） | ✅ 取数点已实锤 |
| [stage-init-regions.md](stage-init-regions.md) | 阶段：Init regions（把地图切成连通区域，只抽 1 个随机数） | ✅ 流程与取数点已实锤 |
| [stage-tiberium.md](stage-tiberium.md) | 阶段：Creating tiberium（布矿） | 🚧 外层已实锤，矿田生长待补 |
| [stage-tech-buildings.md](stage-tech-buildings.md) | 阶段：Adding tech buildings（中立科技建筑，两条互斥分支） | ✅ 已实锤，含 `PickRange` 语义 |
| [stage-recalc.md](stage-recalc.md) | 阶段：绿地蔓延 + Recalculating cell attributes | ✅ 已实锤，附 TileSet 全局↔ini 键名映射 |
| [stage-starting-points.md](stage-starting-points.md) | 阶段：Creating starting points（出生点，**整段可重试**） | ✅ 结构与 3 个取数点已实锤 |
| [stage-hills.md](stage-hills.md) | 阶段：Creating hills（造山，唯一随机源是高度场） | ✅ 随机部分已实锤，抬升子系统待补 |
| [stage-lats.md](stage-lats.md) | 阶段：Creating LATs, rocks etc（地表装饰，取数点最密） | ✅ 18 个取数点全部实锤 |
| [stage-seeding-water.md](stage-seeding-water.md) | 阶段：Seeding water（从整张水里抠陆地） | 🚧 算法骨架 + 随机数账本已还原，缺 tile 序号表 |

## 进度

- [x] 生成器本体定位（`MapSeedClass`，YRpp 符号交叉验证）
- [x] MapGen.cpp 编译单元范围测绘（调试宏反推）
- [x] 生成流水线阶段全景（原版调试日志字符串自带阶段名）
- [x] 随机数发生器逐位复刻 + Google Test 回归
- [x] 原版全局 FPU 状态实锤（`_control87(_RC_CHOP,_MCW_RC)`，控制字 0x0E7F）
- [x] 生成器全局 `Randomizer @ 0xABE890`：**整张地图只播种一次**，种子取自 `.sed`
- [x] 浮点比例 + 拒绝采样取数惯用法复刻 + 对拍（`scaled_random`）
- [x] **`0xABE890` 的使用边界实锤**：153 处引用全在 `0x58B772-0x5AC192`，引擎侧一处没有
- [x] 生成器取数点全清单：**150 处 `Random`、0 处 `RandomRanged`**（按 this 配对）
- [x] **总账落地**：150 处里 46 处是链接器留下的死代码（22 个函数零引用），
      真正会执行的是 **104 处**，且已逐点归到 12 个阶段名下 → [rng-ledger.md](rng-ledger.md)
- [x] `UniformUnit@0x598000` 只经函数指针 `[0xABDFB8+0x10]` 被 `NormalDeviate` 调用；
      正态分布的缓存标志**跨阶段串**，只在每张地图开始时清零
- [ ] 各阶段算法还原（地形/水域/区域/起始点/矿/山丘/装饰）
  - [x] Init random map：**全阶段只消耗 1 个随机数**（`0x59A4C7`，25% 抛硬币）+ 格子/工作数组初值
  - [x] Seeding water：阶段结构 + 水面 tile 细化 `FUN_0059C630` 已还原并对拍
  - [x] Seeding water：涂块核心 `FUN_0059BBC0`（最小堆扩散 + 中心布朗运动）算法已还原
  - [x] 数学基元 `FastSqrt` / 正态分布 / `ChopLog` 复刻并逐位对拍（含架构无关黄金值）
  - [x] Seeding water：三种 MapType 的投放策略 + 区域网格 `FUN_0059A8F0` 已还原
  - [x] Seeding water：岸线平滑 `FUN_0057A0C0` 四遍结构 + **它会消耗随机数**（传递闭包取证）
  - [x] Seeding water：`FUN_0057B210` 八邻域「水」掩码的位定义与三种 mode 门槛已实锤
  - [ ] Seeding water：四遍平滑各分支的 tile 序号表（需 theater 的 TileSet 起始号）
  - [x] Init regions：四步流程 + 唯一取数点 `0x58CAF9`（内联 `PickRange(0,n-1)`）+ 三个谓词已实锤
  - [x] Recalc cell attributes：真正的 `RecalcAttributes` 遍历**不取随机数**；那 1 个随机数来自没有阶段日志的 `0x59B740`（绿地随机蔓延）
  - [x] **TileSet 全局 ↔ theater ini 键名映射**（45 个），`IsWaterOrShore` 的六个区间反查出正好是 WaterSet+ShorePieces+四瀑布，互为佐证
  - [ ] Creating tiberium：外层 0x5A23A0 已实锤（真值表开关 / 出生点重心 / Tiberium x 0.01 插值 / 正负 2 sigma 正态抖动窗口）；内层矿田生长 0x5A28C0 只到骨架
  - [x] Adding tech buildings：`MapType==2` / 其它 两条互斥分支 + 6 个取数点已实锤；业主是 Neutral，建筑表 = `rules.ini [General] NeutralTechBuildings=`
  - [x] Creating starting points：**外层重试循环**（失败一轮的随机数不退回）+ 面积比例配额 + 3 个取数点已实锤
  - [x] Creating hills：入口 `0x5A35F0` 六步已拆开；**全部随机性只在 `0x5A2F50`**（顺序推进的分形高度场，每格 2 次截断正态，`Ruggedness < 10` 时一次都不抽）；高度取整是 round-half-away-from-zero，**不是 ties-to-even**
  - [x] Creating LATs：温带 `0x5A38C0`+`0x5A3AE0` / 非温带 `0x5A4280` 两条互斥分支的主体已实锤；18 个取数点归属到函数；新查实 `0x486790 = IsClearToSandLat`、`0x42B1F0 = (H+4)*W*2`
  - [x] Creating LATs：补丁生长 `0x5A45E0`（最小堆扩散 + `PickRange(1,25)` 选 `TREE01`..`TREE25`，**`TREE00` 永不出现**）与 LAT 写入器 `0x5A4B60` 已还原，**18 个取数点全部落地**
  - [x] 顺手关掉阶段 8 的未确认：`0x71BB90` = `TerrainClass::TerrainClass(type, cell)`（Ghidra 误标为 `WhatAmI`）
  - [ ] Creating hills：把高度场落到地形的角点抬升子系统（`0x6B2A70` / `0x6B4100` / `0x6B4240` / `0x6B3E60` / `0x6B3850`）——无随机，但影响地形字节
- [ ] 📌 **TODO（延后）**：把那 22 个死代码函数（27+ 取数点集中在 `0x5A5020` 一棵子树）单独逆一遍——疑似 TS 时代或 YR 开发期废弃的整套地形生成方案，对"重写红警2"那一层目标有算法参考价值。**前置条件**：104 个活取数点全部还原、端到端逐字节闭环跑通之后再动，详见 [rng-ledger.md](rng-ledger.md) §二
- [ ] `.map` 输出（IsoMapPack5 编码）
- [ ] 端到端验证：同种子生成结果与原版逐字节比对

## 验证方法论：模拟执行取真值

本模块建立了一套**对拍基准**做法（`code/randmap/emu_oracle.py`）：
用 Unicorn 指令级模拟器把 `gamemd.exe` 的节映射到原始基址 `0x400000`，
直接执行原版机器码取返回值，作为独立重写的标准答案。

- 属于对文件的**只读分析**，不修改、不重打包、不分发原版二进制
- 比"进程内映射后直接调用"可靠：64 位 Windows 上宿主进程堆在加载器阶段就已占据
  `0x400000-0x6C0000`（实测 `VirtualAlloc` 返回 `ERROR_INVALID_ADDRESS(487)`，
  连 `/ENTRY` 抢跑都来不及），而模拟器不受宿主地址空间限制
- 基准数据存档在 `memory/data/rmg/`，测试用例把关键数值内联为期望值
- 除了"整函数调用"，还支持**任意代码片段执行**（`Oracle.run_range`）：
  给定起止地址、初始寄存器与 FPU 控制字直接跑一段机器码。含 x87 的取数惯用法
  就是这样取到真值的——不必先把整个阶段的引擎状态都伪造出来

### ⚠️ 模拟器的已知失效面：x87 超越指令

**Unicorn 的 `fyl2x` 完全不理会 FPU 控制字**——`PC=53/RC=chop`、`PC=53/RC=near`、
`PC=64/RC=chop`、`PC=64/RC=near` 四种组合返回**完全相同**的值，实际算的是宿主的
long double。同类的 `fsin` / `fpatan` / `f2xm1` 是否也如此，**未确认**。

因此立下一条规矩：**凡结论依赖 x87 超越指令，必须去真 CPU 上取真值**
（`build/scratch/fyl2x_probe.cpp` 那样的只读原生探针）。
`+ - × ÷` / `fild` / `fstp` / `_ftol` 这些 IEEE 精确运算不受影响，模拟器可信。

更一般的做法：**验证 oracle 的最省事办法是改一个本该改变结果的输入**——
若结果纹丝不动，说明它压根没在模拟那个语义。详见 [fast-math.md](fast-math.md) §四。

### ⚠️ 「这段代码动不动随机数」必须做传递闭包

只搜 `call Randomizer::Random` 会漏掉包装层。岸线平滑的四个函数体内
一处直接调用都没有，差点被写成"平滑不消耗随机数"——实际隔了一层
`0x57ACF0 → 0x598030(PickRange) → Random`，而且它是本阶段消耗的**大头**。

补救办法是 `code/ghidra_scripts/rng_closure.py`：从根函数出发做调用树传递闭包，
并且**用 Ghidra 的真实函数边界（`Function.getBody()`）而不是线性反汇编猜边界**——
同一次扫描，猜边界得到"可达 2525 个函数"（跑飞了），真边界只有 93 个。
结果存档 `memory/data/decomp/rng_closure.txt`。

后来把这套做法又推翻了一次：**Ghidra 的调用图在 MapGen 这段并不完整**
（相当多的函数它根本没定义，图走到那儿就断），而且 `jmp` 跳转表与函数指针
两种边它都不给。最终形态是 `code/randmap/call_closure.py`——
从阶段入口做**指令级递归下降**，记录访问过的每一条指令地址，
解 switch 跳转表，人工补已实锤的函数指针边。三条经验：

1. 判"这段代码取不取随机数"要看 `this`，不是看调了哪个函数
2. 判"这个函数会不会被执行"要往上追引用，**零引用就是死代码**——
   150 个取数点里有 46 个根本不会跑
3. 判"哪个阶段用得到"要用指令级可达性，函数边界靠不住

## 工具箱（本模块新增，均为纯只读 PE 分析，不依赖 Ghidra）

| 脚本 | 作用 |
|------|------|
| `code/randmap/scan_source_macros.py` | 调试宏反推「源文件 ↔ 代码区间」，全 exe 33 个编译单元 |
| `code/randmap/scan_rng_callsites.py` | 扫出所有随机数消耗点并归属到函数/源文件 |
| `code/randmap/disasm.py` | capstone 直读 PE 按 VA 反汇编（含已知符号注释） |
| `code/randmap/xref_abs.py` | 按小端 4 字节搜绝对地址引用（MSVC6 全局变量一律绝对寻址） |
| `code/randmap/emu_oracle.py` | Unicorn 模拟执行取基准（整函数 / 任意片段）**——x87 超越指令不可信** |
| `code/randmap/dump_scaled_golden.py` | 导出浮点取数惯用法的对拍基准 |
| `code/randmap/dump_fastmath_golden.py` | 导出查表 sqrt / 均匀源 / 正态分布的对拍基准 |
| `code/ghidra_scripts/find_writers.py` | 定位 .bss 全局的运行期初始化点（表内容不在文件里） |
| `code/randmap/scan_rmg_rng.py` | 只列**生成器自己那个** Randomizer(0xABE890) 的取数点（按 this 判定） |
| `code/ghidra_scripts/rng_closure.py` | 调用树传递闭包，找出某根函数下**所有**随机数取数点（真函数边界） |
| `code/randmap/dump_tileset_globals.py` | 把 theater 的 45 个 TileSet `.bss` 全局对回 ini 键名（栈槽两段拼接） |
| `code/randmap/read_const.py` | 按 VA 直读 PE 里的 double/float/dword/字节串常量 |
| `code/randmap/call_closure.py` | **指令级**递归下降可达性：把取数点归到阶段、并判出死代码（解 switch 跳转表） |

## 复刻代码的可移植性立场

**产品代码不含任何架构相关指令**，x86 / ARM / 龙芯上必须逐位相同。这一条是硬性要求：
CI 跑在哪种机器上都得给出同一张地图。为此定了三条规矩——

1. **超越函数不用本机指令。** 原版用 x87 `fyl2x` 求 `ln`，但实测证明 `fyl2x` 本身
   **不是正确舍入的**（真 CPU 20000 例中 7 例比精确截断低 1 ulp）。跟着本机指令走
   等于把输出绑死在具体 CPU 实现上。改为定义唯一确定的数学语义
   （「精确 ln 向零截断」，双双精度实现），再**证明**残余差异被下游吸收。
   全过程见 [fast-math.md](fast-math.md) §四。
2. **常量表写死。** 16384 项 sqrt 表硬编码，让"结果与架构无关"在编译期就成立；
   同时用纯整数运算重算全表自检，证明它是数学常量而非搬运的二进制资产。
3. **浮点语义显式加固。** `fp_guard.h` 用 `static_assert` 挡住 x87 超精度、
   有符号右移歧义，用编译选项挡住 FMA 收缩与 `-ffast-math`，
   再用一条运行期自检确认 `fesetround` 真的生效。

`build32/`（`vcvars32` + NMake）保留下来的作用是**取证平台**：只有在 x86 上才能
把复刻结果与真 x87 指令直接对撞，从而给出"我们与原版差多少、差在哪里"的量化答案。
`+ - × ÷` 不需要这么麻烦：SSE2 与 x87-PC53 在同一 RC 下结果相同。
