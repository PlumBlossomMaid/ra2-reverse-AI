# 代码区

可编译的算法重写 + Ghidra 脚本 + 工具脚本。

## 结构

```
code/
├── rewrite/            # C++ 算法重写（生产系统，可独立编译测试）
├── ghidra_scripts/     # Ghidra headless 脚本（标注/反编译/探测）
├── read_constants.py   # PE 常量读取（.rdata 原始字节取证）
├── parse_yrpp.py       # YRpp 头文件 → 符号表
├── parse_phobos_hooks.py  # Phobos 源码 → hook 地址表
├── compare_hooks.py    # hook 与符号对比分析
└── analyze_unknown.py  # unknown 符号分析
```

## rewrite/（算法重写）

当前模块：生产系统（FactoryClass + TimeToBuild）。

编译与测试：

```
# Windows (MSVC)
cl /std:c++17 /utf-8 /W4 production_system.cpp demo.cpp /Fe:demo.exe
# 或 g++ -std=c++17 -Wall production_system.cpp demo.cpp -o demo
demo.exe
```

设计约束：
- **数据驱动**：rules.ini 数值经 BuildRules 注入，算法不硬编码
- 每函数标注原版地址；重写与反编译逻辑一一对应
- 45 项数值断言，CI 自动跑

## ghidra_scripts/（Ghidra 脚本）

运行方式（Ghidra 11.1）：

```
analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis ^
  -postScript <script>.py -scriptPath code/ghidra_scripts
```

| 脚本 | 用途 |
|---|---|
| `apply_symbols.py` | 应用 YRpp 符号（函数 + 全局） |
| `apply_phobos_hooks.py` | 打 Phobos hook 标签 |
| `decompile_*.py` | 导出指定函数反编译 |
| `dump_*_asm.py` | dump 函数汇编 |
| `dump_slots.py` / `dump_vtable.py` | vtable 槽位探测 |
| `verify_vtable.py` | vtable 布局验证 |
| `export_report.py` | 全量分析报告 |
| `probe_addr.py` / `verify_apply.py` / `debug_apply.py` | 标注辅助 |

> 写新脚本前必读 `../memory/notes/ghidra-jython-pitfalls.md`
