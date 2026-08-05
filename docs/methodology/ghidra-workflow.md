# 逆向方法论

本仓库记录的 AI 辅助逆向工作流——符号标注、反编译、取证、重写的完整链路。
每一步都有对应脚本和产出物。

## 标准流程

```
符号对号入座 → 反编译 → 汇编核对 → 常量取证 → 规则映射 → 算法重写 → 数值测试
```

### 1. 符号对号入座

用 YRpp 符号表给 Ghidra 工程的函数/全局打标签。
关键认知：**YRpp 名字是社区 20 年逆向成果的"名册"，本工作是"对号入座"**，
不是从游戏恢复符号。真正的增量贡献在于 YRpp 未覆盖的函数（约 7500 个）。

- 脚本：`code/ghidra_scripts/apply_symbols.py`
- 数据：`memory/data/symbols/yrpp_symbols.tsv`

### 2. 反编译

Ghidra headless + Jython 脚本，导出指定函数的 C 伪代码。

- 模板：`code/ghidra_scripts/decompile_factory.py`
- 陷阱：Jython 2.7 限制很多，见 `memory/notes/ghidra-jython-pitfalls.md`

### 3. 汇编核对

反编译对浮点/条件逻辑可能失真，模糊点必须 dump 汇编逐条还原。

- 案例：`TimeToBuild` 的电力钳制方向（`FCOMPP` 的比较方向容易搞反，
  用数值代入验证最终行为）
- 脚本：`code/ghidra_scripts/dump_timetobuild_asm.py`

### 4. 常量取证

反编译器把全局常量标成 `_DAT_00XXXXXX`，值必须从 PE 文件实测：

- 脚本：`code/read_constants.py`（解析节表，读 `.rdata` 原始字节）
- 关键：**区分 float 和 double**——`0x7E1718` 是 double 1.0，
  按 float 读只有 0.0（前 4 字节为 0）

### 5. 规则映射

把 rules.ini 的配置项与 RulesClass 结构偏移对上：

- 反编译 `RulesClass::Read_General`（`0x66D530`），字段名直接出现在
  INI 读取调用的字符串参数里（如 `s_MinLowPowerProductionSpeed`）

### 6. 算法重写

数据驱动：rules 数值通过 BuildRules 注入，不硬编码。见 `code/rewrite/`。

### 7. 数值测试

每个算法用可验证的数值场景断言。测试程序硬编码原版默认值，
CI 自动运行。见 `code/rewrite/test/`。

## 取证优先级

1. 汇编（最高可信，逐条可核）
2. 反编译（其次，可能有语义失真）
3. YRpp/Phobos 源码（社区成果，作为交叉验证）
4. 推断（必须标注"未确认"）

## AI 协作模式

- AI 负责执行重复的机械工作（脚本、遍历、标注），人类负责判断方向
- 每轮交付必须带证据（地址 + 验证方式），不空口断言
- 发现与社区已知行为矛盾时，以实测为准并记录差异
