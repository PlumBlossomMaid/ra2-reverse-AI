# MapGen.cpp 编译单元测绘与生成流水线全景

## 方法：用调试宏反推编译单元

MSVC 6.0 编译的调试宏在机器码里留下固定序列：

```asm
PUSH <源码行号 imm32>      ; 68 xx xx xx xx
PUSH <源文件名字符串 VA>   ; 68 <va>
CALL 0x734E60
```

直接在 PE 字节里扫 `68 <lineno> 68 <fileVA>` 组合，就能反推出
**"源文件 → 代码地址区间 + 源码行号"**的映射，不需要 Ghidra。
工具：`code/randmap/scan_source_macros.py`。

全 exe 共扫出 **33 个源文件**的代码分布，其中随机地图生成器：

```
0x005956E7 - 0x0059A63A   行号 3934-6746   D:\ra2mdpost\MapGen.cpp   (字符串 @0x82BA48)
```

> 这一步把工作面从"猜测的 0x597000-0x598000 一小段"扩大到**完整的 21KB 编译单元**，
> 是本轮进展的关键杠杆。顺带拿到 `Skirmish.cpp`（遭遇战对话框 @0x6ACC25）等邻近单元。

## 生成器类：MapSeedClass

Ghidra 应用 YRpp 符号后命中 `MapSeedClass::MapSeedClass @ 0x595680`——
生成器类真名是 **`MapSeedClass`**（继承 `LoadOptionsClass`），单例 `Instance @ 0xABDFD8`。

YRpp（`MapSeedClass.h`）给出的字段布局，与本仓库先前**独立逆向出的 17 个偏移完全吻合**
（`Theater +0x38` → `Seed +0x74` → `Description +0x78`），互为验证。详见
[parameters.md](parameters.md)。

## 流水线全景：原版自带阶段名

主流程 `FUN_00598960`（3018 字节）是阶段调度器。它每进入一个阶段就打一条调试日志，
**原版把每个阶段的名字直接写在字符串里**——这等于拿到了生成算法的目录：

| 顺序 | 原版日志 | 实现函数 | 备注 |
|---|---|---|---|
| 0 | — | `Randomizer::Randomizer @ 0x65C6D0` (调用点 `0x598985`) | **种子播种**，一切随机的起点 |
| 1 | `RMG: Init random map` | `0x599650`(4124B) | 单元最大函数 |
| 2 | `RMG: Seeding water` | `0x59A6C0` / `0x59C580` / `0x59C630` | 水域播种 |
| 3 | `RMG: Init regions` | `0x5AC290` / `0x58CF90` / `0x58E740` / `0x58E9B0` / `0x58D010` | 区域初始化 |
| 4 | `RMG: Making regions` | `0x58EBC0` / `0x58EF10` / `0x5A19E0` / `0x578E60` / `0x5A17F0` | 区域生成 |
| 5 | `RMG: Recalculating cell attributes` | `0x59B740` + `CellClass::RecalcAttributes @ 0x47D2B0` | 全程出现 4 次 |
| 6 | `RMG: Creating starting points` | `0x594B50` / `0x5A1FB0` | 玩家出生点 |
| 7 | `RMG: Adding tech buildings` | `0x5A95B0` → `0x595400` | 用 `'Neutral'` + `BuildingClass` 构造中立科技建筑 |
| 8 | `RMG: Creating tiberium` | `0x5A23A0` | 矿脉（沿用 TS 的 tiberium 命名） |
| 9 | `RMG: Creating hills` | `0x5A35F0` | 山丘/高度 |
| 10 | `RMG: Creating LATs, rocks etc` | `0x5A38C0` / `0x5A3AE0` / `0x5A4280` | LAT 过渡贴图、岩石装饰 |
| 11 | `RMG: Cleanup` | `0x722D00` / `0x722240` / `0x5AD790` / `0x568BB0` / `0x42F7C0` | 收尾 |
| 12 | `RMG: Compute Radar Image` | `RadarClass::SetVisibleRect @ 0x654490` / `0x654650` | 雷达图 |
| 13 | `RMG: Done` | — | |

取证：`memory/data/decomp/mapgen_tu_map.txt`（结构测绘）、
`memory/data/decomp/rmg_pipeline_decomp.txt`（各阶段反编译，2610 行）。

> **注意阶段顺序即种子兼容性**：每个阶段消耗的随机数个数必须与原版一致，
> 少取一次就会让后续所有阶段错位。复刻时要按此表逐阶段对齐随机数消耗。

## 调用入口

- 对话框路径：`FUN_00596300`（RMG 参数对话框消息处理，满屏 `GetDlgItem`/`EnableWindow`）
  在 `0x59664C` / `0x596A49` / `0x596A66` 三处调用主流程
- 开局路径：`FUN_00684620`（场景加载层）在 `0x684989` 调用主流程
- 对话框初始化 `FUN_00595BC0` 用 `RawFileClass` 读 **`RandMap.img`**，
  且与 `PreviewClass` 成对出现 → **`RandMap.img` 是地图预览图，不是地形模板**
  （实测其体积随地图参数变化：51,859B → 15,875B）

## 相关外部实现

- **EA 官方任务编辑器**（`CNC_TS_and_RA2_Mission_Editor`，GPL-3.0）：`.map` 读写的权威实现，
  但**不含随机地图生成器**
- **RA1 源码**（EA 2020 开源）：**不含**随机地图生成器（这是 TS/RA2 引入的特性），
  仅底层随机数/地形工具可作理解辅助
