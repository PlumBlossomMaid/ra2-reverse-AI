# 生产系统逆向（FactoryClass）

RA2 的生产/建造系统——游戏核心经济逻辑。本目录记录对原版 `gamemd.exe`
中生产系统的完整逆向，从反编译证据到可编译的算法重写。

## 文章

| 文档 | 内容 |
|---|---|
| [factory-class.md](factory-class.md) | FactoryClass 状态机：16 个成员函数逐一定义 |
| [time-to-build.md](time-to-build.md) | TechnoClass::TimeToBuild 建造时间完整公式 |
| [address-map.md](address-map.md) | 函数地址映射 + 对象内存布局 |

## 代码与验证

- 算法实现：`../../code/rewrite/`（production_system.h/cpp）
- 数值测试：`../../code/rewrite/test/test_production.cpp`（Google Test，6 组用例）
- 原始取证：`../../memory/data/decomp/`（反编译 txt + 汇编 txt）

## 核心结论速览

- 生产时钟固定 **54 步**（`0x36`），`Rate` 为每步帧数，`Value` 累计到 54 完成
- **每步扣款** = `Balance / (54 − Value)`；`Balance` 语义为"剩余未付欠款"
- **放弃生产退款** = `实际成本 − Balance`
- **建造帧数** = 基础 × 难度 × 类型系数 ÷ 电力系数 × `MultipleFactory^(工厂数−1)` × 围墙系数
- 断电时建造时间翻倍（电力系数下限 0.5）
