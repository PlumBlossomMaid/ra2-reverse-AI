# 生产系统地址映射

所有地址为 RVA（与 YRpp 地址体系一致），基于 `gamemd.exe`（ImageBase `0x400000`）。

## 函数地址

| 地址 | 函数 | 来源 |
|---|---|---|
| `0x4C98B0` | FactoryClass::FactoryClass | YRpp |
| `0x4C9C60` | FactoryClass::HasProgressChanged | YRpp |
| `0x4C9C70` | FactoryClass::DemandProduction | YRpp |
| `0x4C9E10` | FactoryClass::SetObject | YRpp（Ghidra 未建函数） |
| `0x4C9E60` | FactoryClass::Suspend | YRpp |
| `0x4C9EA0` | FactoryClass::Unsuspend | YRpp |
| `0x4C9FB0` | FactoryClass::GetBuildTimeFrames | YRpp |
| `0x4C9FF0` | FactoryClass::AbandonProduction | YRpp |
| `0x4CA120` | FactoryClass::GetProgress | YRpp |
| `0x4CA130` | FactoryClass::IsDone | YRpp |
| `0x4CA180` | FactoryClass::GetCostPerStep | YRpp（Ghidra 需强制建函数） |
| `0x4CA1A0` | FactoryClass::CompletedProduction | YRpp |
| `0x4CA5A0` | FactoryClass::StartProduction | YRpp |
| `0x4CA620` | FactoryClass::RemoveOneFromQueue | YRpp |
| `0x4CA670` | FactoryClass::CountTotal | YRpp |
| `0x4CA6B0` | FactoryClass::IsQueued | YRpp |
| `0x6F47A0` | TechnoClass::TimeToBuild | YRpp |
| `0x4FCE30` | HouseClass::GetPowerPercentage | YRpp |
| `0x500910` | HouseClass::GetFactoryCount | Phobos hook 定位 |
| `0x66D530` | RulesClass::Read_General | YRpp |

## 全局数据

| 地址 | 内容 |
|---|---|
| `0x8871E0` | RulesClass::Instance（YRpp 确认） |
| `0x7E1718` | double 1.0（满电返回值） |
| `0x7E2800` | double 0.0（断电返回值） |
| `0x7E2AC8` | float 1.0 |
| `0x7E1748` | float 0.0 |
| `0x7F4E34` | float 0.01（防除零） |
| `0xA83E30` | FactoryClass::Array（全局工厂数组） |

## vtable

| 地址 | 说明 |
|---|---|
| `0x7F4960` | TechnoClass vtable（抽象基类，含纯虚桩 `0x4C9150`） |
| `0x7E88D0` | FactoryClass vtable（构造函数 `*this = &PTR_FUN_007e88d0`） |

TechnoClass vtable 关键槽位：

| 槽位 | 目标 | 语义 |
|---|---|---|
| `+0x2C` | `0x4C9150` | 纯虚桩 |
| `+0x84` | `0x6F3270` | GetType 相关 thunk |
| `+0x88` | `0x4E0130` | 基础建造帧数取数链 |

## 对象内存布局（生产系统相关）

### FactoryClass

| 偏移 | 字段 | 反编译证据 |
|---|---|---|
| `+0x24` | Production.Value | CompletedProduction 等 |
| `+0x2C/0x30/0x34` | Timer | Suspend 重置 |
| `+0x38` | Production.Rate | Unsuspend 写入 |
| `+0x44` | 队列 Items | StartProduction 取首元素 |
| `+0x48` | 队列 Capacity | DemandProduction 判满 |
| `+0x50` | 队列 Count | DemandProduction 计数 |
| `+0x54` | 队列 CapacityIncrement | 扩容 |
| `+0x58` | Object | 各函数 |
| `+0x5D` | IsDifferent | HasProgressChanged |
| `+0x60` | Balance | GetCostPerStep |
| `+0x68` | SpecialItem | IsDone |
| `+0x6C` | Owner | DemandProduction |
| `+0x70` | IsSuspended | Suspend |
| `+0x71` | IsManual | Suspend |

### HouseClass

| 偏移 | 字段 | 证据 |
|---|---|---|
| `+0x53A4` | 可用电力 | GetPowerPercentage |
| `+0x53A8` | 电力需求 | GetPowerPercentage |

### TechnoClass

| 偏移 | 字段 | 证据 |
|---|---|---|
| `+0x21C` | Owner/类型索引 | TimeToBuild 传参 |
| `+0x520` | 墙标志源对象 | TimeToBuild 步骤 6 |
| `+0x300` | Object.Balance | DemandProduction 写入 |

> 注：`+0x520`→`+0x1571` 为墙标志的完整链路（`this[0x520]` 指向的对象 + `0x1571`）。
