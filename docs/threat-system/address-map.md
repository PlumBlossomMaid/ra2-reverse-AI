# 威胁系统地址映射

所有地址为 RVA（ImageBase `0x400000`），基于 `gamemd.exe` 原版。

## 函数地址

| 地址 | 函数 | 来源 |
|---|---|---|
| `0x70CD10` | TechnoClass::ThreatCoefficients | YRpp |
| `0x708B40` | TechnoClass::CalculateThreat（vtable[0x2C0]） | vtable 定位 |
| `0x70F6E0` | TechnoClass::UpdateThreatInCell | YRpp |
| `0x481870` | CellClass::UpdateThreat | YRpp |
| `0x509400` | HouseClass::AdjustThreats | YRpp |
| `0x509130` | HouseClass::AcquiredThreatNode | YRpp |
| `0x56BCD0` | MapClass::GetThreatPosed | YRpp |
| `0x6F7CA0` | TechnoClass::CanAutoTargetObject | YRpp |
| `0x6F8960` | TechnoClass::TryAutoTargetObject | YRpp |
| `0x6F8DF0` | TechnoClass::GreatestThreat | YRpp |
| `0x4D9920` | FootClass::GreatestThreat（包装层） | YRpp |
| `0x772A90` | WeaponTypeClass::AllowedThreats | YRpp |
| `0x5F5C60` | FUN（血量比例，强度输入） | 反编译定位 |
| `0x4CAC40` | FUN（快速 sqrt） | 反编译定位 |

## 关键 vtable 槽位（TechnoClass vtable @ 0x7F4960）

| 槽位 | 语义 |
|---|---|
| `+0x2C0` | CalculateThreat → `0x708B40` |
| `+0x2E4` | 获取武器槽（GetWeapon 链） |
| `+0x3F8` | 获取武器指针 |
| `+0x408` | 载员数（建筑） |
| `+0x84` | GetType |

## 数据结构

### TechnoClass（威胁相关）

| 偏移 | 字段 |
|---|---|
| `+0x6C` | 当前血量 Strength |
| `+0x2B4` | 当前瞄准目标 Target |
| `+0x21C` | Type |
| `+0x142*4` | ThreatPosed（威胁贡献） |
| `+0x1B0*4` | 某类型指针（+0xEC3 等标志位源） |

### TechnoTypeClass

| 偏移 | 字段 |
|---|---|
| `+0x1FB` | 使用类型覆盖系数标志 |
| `+0x9C` | 装甲类型索引（Verses 表索引） |
| `+0xA0` | 最大血量 |
| `+0x2C0` | SpecialThreatValue |
| `+0x2C8` | MyEffectivenessCoefficient |
| `+0x2D0` | TargetEffectivenessCoefficient |
| `+0x2D8` | TargetSpecialThreatCoefficient |
| `+0x2E0` | TargetStrengthCoefficient |
| `+0x2E8` | TargetDistanceCoefficient |
| `+0x394` | 特殊单位类型标志（威胁修正） |
| `+0x5B8` | 无武器时评估射程 |
| `+0x670` | ThreatPosed（类型基础威胁值） |

### WarheadType

| 偏移 | 字段 |
|---|---|
| `+0xA0` | Verses 伤害表（double 数组，索引=装甲） |
| `+0x2A4` | 防空弹头标志（AllowedThreats 用） |
| `+0x2A5` | 特殊目标弹头标志 |

### WeaponType

| 偏移 | 字段 |
|---|---|
| `+0xAC` | Warhead 指针 |
| `+0xB4` | Range（射程，÷256 使用） |

### HouseClass

| 偏移 | 字段 |
|---|---|
| `+0x1FB` | 威胁节点标记（AcquiredThreatNode 设置） |
| `+0x57E4` | ThreatPosedEstimates[130][130]（威胁矩阵） |
| `+0x5788` | 敌我关系位图（AdjustThreats 过滤用） |

### RulesClass（Read_General 反编译确认）

| 偏移 | 字段 |
|---|---|
| `+0x1040`~`+0x1064` | 5 个非 Dumb 默认系数 |
| `+0x1068`~`+0x108C` | 5 个 Dumb 默认系数 |
| `+0x1090` | EnemyHouseThreatBonus |
| `+0xDF4` | ThreatPerOccupant |
| `+0x16F8`/`+0x1708` | CanAutoTargetObject 中血量比例比较基准 |

## 全局数据

| 地址 | 内容 |
|---|---|
| `0x00A8E3A0` / `0x00A8E394` | 建筑数组（GreatestThreat 层1） |
| `0x00A8EC88` / `0x00A8EC7C` | 单位数组（GreatestThreat 层2） |
| `0x008243C8` | AdjustThreats 邻域偏移表（9 int） |
| `0x008243EC` | AdjustThreats 邻域移位表（9 int） |
| `0x00A80238` / `0x00A8022C` | House 数组（CellClass::UpdateThreat 遍历） |
| `0x007E2800` | double 0.0（无威胁返回） |
| `0x007F4E90` | float 0.0（公式末尾加项） |
| `0x00B0EA90` | 哨兵坐标（"用目标位置"标记） |
