# 存档系统区

YR 存档（.sav）格式逆向——OLE CFB 复合文档 + CONTENTS 对象序列化。

## 索引

| 文档 | 内容 | 状态 |
|---|---|---|
| [sav-format.md](sav-format.md) | .sav 完整格式：CFB 外壳 + SavegameInformation 属性 + 加载/保存主流程 + 对象序列化格式 + SwizzleManager + V3 篡改点 + 未完成清单 | ✅ 三层完成（外壳 / 加载流程 / 对象格式） |

## 未完成（详见 sav-format.md 末尾"未完成 / 待验证清单"）

**静态可挖（无需开游戏）：**
- CONTENTS 头部逐字节解析（地图名后的结构化头部）
- 26+ 对象数组批次的完整类型映射（过半仍是"推测内容"）
- BuildingClass / InfantryClass / HouseClass 的 Save 调用链（方法论已跑通）
- SwizzleManager::Process 对无映射条目地址的处理（FUN_006CF350）

**需开游戏验证（挂起中）：**
- 实际篡改实验（diff 两局 CONTENTS，验证 V3 核弹理论）
- 崩溃复现抓地址（对照 0x51BB7A / 0x71ADE0 / 0x71B151）
