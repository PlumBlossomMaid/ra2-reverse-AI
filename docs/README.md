# 文档区

逆向分析文章。按机制组织，每篇以"地址证据 → 算法 → 验证"结构撰写。

## 索引

| 目录 | 内容 | 状态 |
|---|---|---|
| [production-system/](production-system/) | 生产/建造系统（FactoryClass + TimeToBuild） | ✅ 已完成 |
| [threat-system/](threat-system/) | 威胁评估系统（公式 + 威胁地图 + 索敌） | ✅ 已完成 |
| [methodology/](methodology/) | 逆向方法论（Ghidra 工作流、AI 协作） | 🟡 进行中 |
| [symbols/](symbols/) | 三层符号标注成果说明 | ✅ 已完成 |
| [bug-triage/](bug-triage/) | 崩溃排查（超时空移除可驻军建筑） | 🟡 待运行时地址确认 |
| [save-game/](save-game/) | YR 存档格式（OLE CFB + CONTENTS 序列化） | 🟡 三层完成，余 8 项盲区已入档 |
| [csf-format/](csf-format/) | RA2/YR 字符串资源（FSC + LBL + UTF-16LE 取反） | ✅ 已完成 |
| [mix-format/](mix-format/) | MIX 打包格式（指向外部工具 ccmix，GPL v2+） | 🟡 工具索引 |
| [references/](references/) | 外部参照（EA 开源 C&C 源码 = YR 逆向对照实现） | 🟡 已建 3 机制对照 |

## 写作规范

- 每个算法必须给出原版地址（RVA）和验证方式（反编译 / 汇编 / 数值测试）
- 不确定的部分显式标注"未确认"，不猜测
- 数值类结论附测试用例编号（`code/rewrite/test/`，Google Test）
