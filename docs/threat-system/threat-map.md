# 威胁地图 —— 单位威胁贡献与传播

威胁地图是 RA2 AI 的"兵力感知"：每个单位把自己的威胁值写到所在格子，
传播到每个阵营的威胁估计矩阵，AI 据此判断"哪里危险"。

## 数据流

```
CalculateThreat() → ThreatPosed (单位自身)
      ↓
UpdateThreatInCell() → CellClass::UpdateThreat() → 各 House 威胁矩阵
      ↓
HouseClass::AdjustThreats() → 3×3 邻域模糊扩散
      ↓
MapClass::GetThreatPosed() → AI 查询某格威胁
```

## CalculateThreat @ 0x708B40 —— "珍宝函数"

YRpp 注释：`...this is another gem of a function, to be revealed another time...`

```
int CalculateThreat(TechnoClass* unit) {
    if (unit 是建筑) {
        if (载员数 > 0)     return 载员数 × ThreatPerOccupant;   // Rules+0xDF4
        if (有驻防单位)     return 驻防单位类型.ThreatPosed;
        return 类型.ThreatPosed;                                   // +0x670
    }
    return 类型.ThreatPosed;                                       // +0x670
}
```

- `Type->ThreatPosed`（+0x670）= rules.ini 单位属性的 `ThreatPosed` 值
- **Phobos 交叉验证**：`Hooks.cpp:2004` 对同一地址 hook：
  `return RulesClass::Instance->ThreatPerOccupant * occupantCount;`
- 语义：**一座装满兵的多功能步兵车/基地很危险**——威胁值随载员线性增长

## UpdateThreatInCell @ 0x70F6E0

```
void UpdateThreatInCell(TechnoClass* unit, CellClass* cell) {
    if (!cell) return;
    cell->UpdateThreat(-unit->ThreatPosed);   // 移除旧威胁
    unit->ThreatPosed = 0;
    unit->ThreatPosed = unit->CalculateThreat();   // 重新计算
    cell->UpdateThreat(unit->ThreatPosed);         // 写入新威胁
}
```

单位移动/状态变化时调用，保持威胁图与单位状态同步。

## CellClass::UpdateThreat @ 0x481870

```
void CellClass::UpdateThreat(int cellThreat, HouseClass* pHouse) {
    // 遍历所有 House (DAT_00A80238):
    //   非我方 House → FUN_004FA2E0 传播威胁
}
```

威胁按阵营传播——每个 House 维护自己对地图的威胁认知。

## HouseClass::AdjustThreats @ 0x509400 —— 模糊扩散

遍历所有单位，把每个单位的威胁值按 **3×3 邻域** 扩散到威胁估计矩阵
（`ThreatPosedEstimates[130][130]`，HouseClass+0x57E4）：

```
偏移表 DAT_008243C8:  {-0x83, -0x82, -0x81, -0x01, 0x00, 0x01, 0x81, 0x82, 0x83}
移位表 DAT_008243EC:  {  2,    1,    2,    1,    0,   1,   2,    1,    2   }

对每个邻域格: 威胁 += unitThreat >> shift   (2^shift 递减)
```

| 邻域位置 | 相对偏移 | 移位 | 权重 |
|---|---|---|---|
| 中心 | 0 | 0 | 1/1 |
| 上下左右 | ±1, ±0x80 | 1 | 1/2 |
| 四角 | ±0x81, ±0x83 | 2 | 1/4 |

**这是"AI 又聪明又笨"的又一证据**：AI 知道"这个区域有威胁"，
但位置是模糊的（中心最精确，边缘衰减）。

## HouseClass::AcquiredThreatNode @ 0x509130

```
void AcquiredThreatNode() { House+0x1FB = 1; }
```
威胁节点标记（YRpp 注释：节点建筑死亡时也会调用——BUG）。

## 数据结构

| 对象 | 偏移 | 字段 |
|---|---|---|
| HouseClass | `+0x57E4` | ThreatPosedEstimates[130][130]（威胁矩阵，int） |
| TechnoClass | `+0x142*4` | ThreatPosed（当前威胁贡献） |
| TechnoTypeClass | `+0x670` | ThreatPosed（类型基础威胁值） |
| RulesClass | `+0xDF4` | ThreatPerOccupant（每载员威胁） |

## 验证（Google Test）

| 场景 | 结果 |
|---|---|
| 单位威胁 = Type.ThreatPosed | ✅ |
| 建筑威胁 = 载员 × ThreatPerOccupant | ✅ 4×50=200 |
| 驻防建筑威胁 = 驻防单位类型威胁 | ✅ |
| 空建筑威胁 = 自身类型威胁 | ✅ |
