# 存档系统区

YR 存档（.sav）格式逆向——OLE CFB 复合文档 + CONTENTS 对象序列化。

## 索引

| 文档 | 内容 | 状态 |
|---|---|---|
| [sav-format.md](sav-format.md) | .sav 完整格式：CFB 外壳 + SavegameInformation 属性 + CONTENTS 序列化 + 跨 mod 原理 | ✅ 第一层完成 |

## 待挖

- CONTENTS 内部完整布局（对象批次顺序、Swizzle 表格式）
- 实际制作逆天存档的验证实验（diff 定位修改点）
- 加载流程（ScenarioClass::LoadGame @ 0x67E440 区）反编译
