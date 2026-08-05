# 文档区

逆向分析文章。按机制组织，每篇以"地址证据 → 算法 → 验证"结构撰写。

## 索引

| 目录 | 内容 | 状态 |
|---|---|---|
| [production-system/](production-system/) | 生产/建造系统（FactoryClass + TimeToBuild） | ✅ 已完成 |
| [methodology/](methodology/) | 逆向方法论（Ghidra 工作流、AI 协作） | 🟡 进行中 |
| [symbols/](symbols/) | 三层符号标注成果说明 | ✅ 已完成 |

## 写作规范

- 每个算法必须给出原版地址（RVA）和验证方式（反编译 / 汇编 / 数值测试）
- 不确定的部分显式标注"未确认"，不猜测
- 数值类结论附测试用例编号（`code/rewrite/demo.cpp`）
