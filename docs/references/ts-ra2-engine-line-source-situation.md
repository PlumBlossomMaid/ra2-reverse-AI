# TS/RA2 引擎线源码状况与可借力点（2026-10-08 情报）

## 结论

**TS（泰伯利亚之日）→ RA2 这条引擎线，是 EA 历次 C&C 开源中唯一被整体跳过的分支。**
最可能的解释：这条线的源码本身不完整/已丢失（客观开不了），RA2 作为 TS 的直系
升级版、没有 TS 打底也开不成（主观上也就没必要单独挣扎）。

## 证据链

- **2020 年**（C&C Remastered Collection）：EA 开源 **C&C1（Tiberian Dawn）+ RA1**，GPL v3。
- **2025 年 2 月**：EA 追加开源 **Tiberian Dawn / Red Alert / Renegade / Generals(+Zero Hour)**，
  见 [AUTOMATON 报道](https://automaton-media.com/articles/newsjp/strategy-game-commandconquer-code-20250305-330468/)、
  [unwire.hk](https://www.unwire.hk/2025/03/01/ea-just-released-source-code-for-a-bunch-of-old-command-and-conquer-games/game-channel/)。
- 两批名单里都**没有 TS、没有 RA2/YR**。RA2 移植项目文档原话：
  "[**RA2 has no released engine source**](https://github.com/ammaarreshi/RedAlert2-Mac-iOS-iPad/blob/main/README.md)"。
- 按 EA"能开尽开"的节奏（连 Renegade、Generals 都开了），唯独跳过人气最高的 RA2，
  指向代码状况问题而非意愿问题；社区多年传闻（Westwood 搬迁/备份事故致 voxel 时代引擎源码残缺）
  与此自洽。Petroglyph 当年重制只接 C&C1/RA1、RA2 重制至今无影，亦是旁证。

## 关键情报：OpenTS 项目

[OpenTS-Developers/OpenTS](https://github.com/OpenTS-Developers/OpenTS)
（[HISTORY.md](https://github.com/OpenTS-Developers/OpenTS/blob/main/docs/HISTORY.md)）——
社区在做 **Tiberian Sun 引擎重建**，路线与本仓库互补：

> EA's source releases for related Command & Conquer titles contain much of Tiberian Sun's
> foundation, but not its finished engine. The reconstruction began with the GPL-licensed
> Red Alert source (2020), then moved to the fuller Red Alert source released with
> The Ultimate Collection (2025).

即：**用 RA1 GPL 代码里共享的 Westwood 公共库打底，重建 TS**。

## 对本仓库（RA2/YR 恢复）的可借力点

1. **RA2 = TS 引擎升级版，TS ≈ RA1 引擎演化**——三层之间共享 Westwood 公共库
   （Westwood 跨项目复用代码出了名）。RA1 GPL 源码和 OpenTS 的重建成果是两座现成的桥。
2. **先对齐再逆向**：对 RA2 反编译到的函数，先查 RA1 GPL 源码与 OpenTS 代码库有无对应实现——
   能对上的直接引用（GPL），把逆向工作量集中在**真正丢失、TS/RA2 独有的部分**
   （voxel 渲染、RA2 新增系统、YR 新增机制）。
3. **代码组织参照**：OpenTS 的模块划分方式可作为本仓库 `code/rewrite/` 引擎级重写的
   结构参考，避免从零设计引擎骨架。
4. 本仓库 `docs/references/cncremastered-source.md` 已有 RA1 对照关系记录，
   本条情报是其补充：RA1 对照解决"公共库"，OpenTS 解决"TS 层"，剩下的才是 RA2 独有。

## 注意

- "TS/RA2 源码丢失"是**社区共识级推断，不是官方确认**——EA 从未明说原因。
  行动按"拿不到原版 TS/RA2 源码"规划即可，若未来 EA 突然开源（2025 年那批就是意外之喜），
  属于纯赚。
- 引用 GPL 代码时注意许可证义务（GPL v3 + EA 附加条款），重写与引用要分清。

## 补充：开源边界的两种"不开"（2026-10-08 讨论）

按年份排开，"没开源"其实是两种原因，TS/RA2 的归属一目了然：

| 游戏 | 年份 | 开源？ | 原因 |
|---|---|---|---|
| C&C1 / RA1 | 1995/96 | ✅ | 老，无残值 |
| **TS / RA2** | **1999/2001** | ❌ | **异常点：够老却被跳过 → 指向源码残缺** |
| Renegade | 2002 | ✅ | 老，无残值 |
| Generals | 2003 | ✅ | 老，无残值 |
| C&C3 / RA3 / C&C4 | 2007-2010 | ❌ | SAGE 引擎一鱼多吃（含 BFME 系列），仍有许可价值，主动捂着 |

关键点：**RA2（2001）比已开源的 Renegade（2002）、Generals（2003）更老**，
"太新不能开"罩不住它——它完整落在"够老该开"的区间内却被精准跳过。
开源边界划在 Generals（2003）：边界内的 TS/RA2 是被挖掉的洞（代码残缺），
边界外的 SAGE 世代是正常的商业保留（还有价值）。两种"不开"互相印证。
