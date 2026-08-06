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

## 进度

- [x] 生成器本体定位（`MapSeedClass`，YRpp 符号交叉验证）
- [x] MapGen.cpp 编译单元范围测绘（调试宏反推）
- [x] 生成流水线阶段全景（原版调试日志字符串自带阶段名）
- [x] 随机数发生器逐位复刻 + Google Test 回归
- [x] 原版全局 FPU 状态实锤（`_control87(_RC_CHOP,_MCW_RC)`，控制字 0x0E7F）
- [x] 生成器全局 `Randomizer @ 0xABE890`：**整张地图只播种一次**，种子取自 `.sed`
- [x] 浮点比例 + 拒绝采样取数惯用法复刻 + 对拍（`scaled_random`）
- [ ] 各阶段算法还原（地形/水域/区域/起始点/矿/山丘/装饰）
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

## 工具箱（本模块新增，均为纯只读 PE 分析，不依赖 Ghidra）

| 脚本 | 作用 |
|------|------|
| `code/randmap/scan_source_macros.py` | 调试宏反推「源文件 ↔ 代码区间」，全 exe 33 个编译单元 |
| `code/randmap/scan_rng_callsites.py` | 扫出所有随机数消耗点并归属到函数/源文件 |
| `code/randmap/disasm.py` | capstone 直读 PE 按 VA 反汇编（含已知符号注释） |
| `code/randmap/xref_abs.py` | 按小端 4 字节搜绝对地址引用（MSVC6 全局变量一律绝对寻址） |
| `code/randmap/emu_oracle.py` | Unicorn 模拟执行取基准（整函数 / 任意片段） |
| `code/randmap/dump_scaled_golden.py` | 导出浮点取数惯用法的对拍基准 |
