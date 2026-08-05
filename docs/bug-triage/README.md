# 崩溃排查区

原版 gamemd.exe 运行时崩溃问题的定位与分析。每篇以"场景 → 原版取证 → 崩溃点假设 → 验证"结构撰写。

## 索引

| 文档 | 场景 | 状态 |
|---|---|---|
| [temporal-building-warp-crash.md](temporal-building-warp-crash.md) | 步兵 Enter 中 → 超时空 warp out 建筑 → 崩溃 | 🟡 待运行时地址确认 |

## 规范

- 每个崩溃点必须给出原版地址（RVA）+ 反编译/汇编取证
- 引用社区修复（Phobos/Ares hook）时必须附源码位置交叉验证
- 未确认的崩溃路径标注"假设"，不写成结论
