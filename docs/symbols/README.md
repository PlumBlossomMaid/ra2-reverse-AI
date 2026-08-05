# 三层符号标注

对 `gamemd.exe` 的 Ghidra 全量分析（8637 个函数）完成三层符号标注。
这是后续机制逆向的地基——"名字对号入座，机制才可读"。

## 标注内容

| 层 | 数量 | 来源 |
|---|---|---|
| YRpp 函数名 | 1106 | apply_symbols.py（两轮：857 + 249） |
| YRpp 全局变量 | 251 | apply_symbols.py |
| Phobos hook 注入点 | 1468 | apply_phobos_hooks.py |

## 数据文件（`memory/data/symbols/`）

| 文件 | 说明 |
|---|---|
| `yrpp_symbols.tsv` | 1612 符号（1361 函数 + 251 全局，unknown 仅 21） |
| `phobos_hooks.tsv` | 1468 个 Phobos DEFINE_HOOK 地址 |
| `named_functions.txt` | 933 个命名函数清单（验证用） |

## 覆盖分析

- 1468 个 Phobos hook 中：40 个精确命中 YRpp 符号，1428 个位于函数内部
- YRpp 未覆盖函数约 7500 个（网络同步、冷门机制等）——
  **这是本项目的真正增量机会**：挖机制、写文档、起名

## Hook 密度高的区域（机制挖掘候选）

| 区域 | 关注点 |
|---|---|
| `0x418000` | Aircraft |
| `0x469000` | BulletClass |
| `0x489000` | DamageArea（弹道伤害，22 hook） |
| `0x6F7000-0x701000` | TechnoClass 核心（16 hook） |
| `0x73D000` | UnitClass Harvesting（采矿，13 hook） |
