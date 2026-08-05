# ra2-reverse-AI

用 **Qwen Code**（AI 编程助手）对《命令与征服：红色警戒 2 尤里的复仇》进行逆向工程的
完整资产库——文档、可编译代码、CI、以及逆向过程的知识沉淀。

> 目标二进制：`gamemd.exe`（2001-10-31 原版，MSVC 6.0，x86，ImageBase `0x400000`）

## 为什么做这个

- 社区已有 20 年逆向成果（YRpp 符号体系、Ares/Phobos 扩展），但**原版二进制 + 符号 + 反编译 + 算法重写**的完整链路鲜有公开
- 目标不是复刻游戏，而是**挖出机制、用高级语言重写、回馈社区**——像一个可以持续扩展的"游戏机制百科"
- 本仓库同时记录 **AI 辅助逆向的工作流**：符号标注 → 反编译 → 汇编核对 → 算法重写 → 数值验证

## 目录结构

```
ra2-reverse-AI/
├── README.md            ← 本文件：入口与导航
├── QWEN.md              ← Qwen Code 工作手册（索引、工作流、规范）
├── docs/                ← 文档区：逆向分析文章
│   ├── production-system/   # 生产系统（已完成首轮全量逆向）
│   ├── methodology/         # 逆向方法论（Ghidra 工作流、AI 协作模式）
│   └── symbols/             # 符号标注成果说明
├── code/                ← 代码区：可编译的算法重写 + Ghidra 脚本
│   ├── rewrite/             # C++ 算法重写（当前：生产系统）
│   ├── ghidra_scripts/      # Ghidra headless 脚本（标注/反编译/探测）
│   └── *.py                 # 符号解析、PE 常量读取等工具
├── ci/                  ← CI 设计与文档（workflow 位于 .github/workflows/）
├── memory/              ← 记忆区：踩坑记录、地址笔记、原始取证数据
│   ├── notes/               # 知识笔记（Ghidra Jython 踩坑等）
│   └── data/                # 原始数据（符号表、反编译输出、B站参考资料）
└── .github/workflows/   ← GitHub Actions（编译 rewrite + 跑测试）
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
- [ ] 更多机制挖掘（弹道伤害、采矿、AI 等）
- [ ] 发布文章整理

## 版权

本仓库不含任何原版游戏代码或素材。地址信息来源于社区公开的 YRpp（GPL）。
算法重写为独立实现，重新发布请遵守相关社区规范。
