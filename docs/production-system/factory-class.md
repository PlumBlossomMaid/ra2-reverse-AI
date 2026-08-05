# FactoryClass 生产状态机

对应原版地址 `0x4C98B0`–`0x4CA6B0`。全部 16 个成员函数经 Ghidra 反编译 + 汇编核对，
算法重写见 `code/rewrite/`。

## 状态字段（内存布局）

| 偏移 | 字段 | 语义 |
|---|---|---|
| `+0x24` | `Production.Value` | 当前进度（0..54） |
| `+0x2C` | `Production.Timer` | CDTimerClass（12 字节） |
| `+0x38` | `Production.Rate` | 每步帧数 |
| `+0x40` | `QueuedObjects.Items` | 队列元素数组 |
| `+0x50` | `QueuedObjects.Count` | 队列长度 |
| `+0x58` | `Object` | 当前生产物（TechnoClass*） |
| `+0x5D` | `IsDifferent` | 进度变化标志 |
| `+0x60` | `Balance` | **剩余未付欠款** |
| `+0x68` | `SpecialItem` | 特殊生产项，-1=无 |
| `+0x6C` | `Owner` | 所属 House |
| `+0x70` | `IsSuspended` | 挂起标志（没钱/手动/完成后待部署） |
| `+0x71` | `IsManual` | 挂起是否玩家手动触发 |

## 成员函数

### GetBuildTimeFrames `@ 0x4C9FB0`
```
frames = Object ? Object->TimeToBuild() : 0
frames /= 54
return clamp(frames, 1, 255)
```
出兵卡图标的步进帧率。钳制到 `[1, 255]`。

### GetCostPerStep `@ 0x4CA180`
```
if (!Object) return 0
remaining = 54 - Value
return remaining ? Balance / remaining : Balance
```
**每帧扣款**。`Balance` 是剩余欠款，平均摊到剩余步数。完成时返回剩余 Balance。

### Suspend `@ 0x4C9E60`
未挂起时：记录 `IsManual`，置 `IsSuspended`，`Rate=0`，重置计时器。

### Unsuspend `@ 0x4C9EA0`
```
条件: (有生产物或特殊项) && 挂起中 && 未完成
恢复: IsSuspended=false
Rate = clamp(TimeToBuild()/54, 1, 255)
每步成本 = Object ? (54-Value ? Balance/(54-Value) : Balance) : 0
if (每步成本 <= 可用资金) 恢复成功
```
注意原版 quirk：函数入口即清除挂起标志，资金不足也返回 false。

### CompletedProduction `@ 0x4CA1A0`
`Value == 54` 时：清除生产物/特殊项，置挂起（等待部署），进度清零。
特殊项判定用 `!= 0`（与 IsDone 的 `!= -1` 不同）。

### IsDone `@ 0x4CA130`
`(Object != null || SpecialItem != -1) && Value == 54`。

### AbandonProduction `@ 0x4C9FF0`
```
退款 = Object->GetActualCost() - Balance
Balance = 0; 特殊项 = -1; 进度清零; 挂起
AI 玩家额外清理对应类型的生产标记
释放 Object
```

### DemandProduction `@ 0x4C9C70`
三路分支：
1. 请求物为 **BuildingType** → 先 `AbandonProduction()`（同类型替换）
2. 立即生产：工厂空闲 或 `startNow` 参数
   - 空闲判定：`(Rate==0 || 挂起) && 队列空 && (Object==null || !挂起)`
   - 创建生产物，`Balance = GetActualCost()`
3. 入队：队列满（`BuildQueueCap`）或已排队 → 拒绝并播放提示音

### StartProduction `@ 0x4CA5A0`
队列非空且无生产物时：弹出队首，创建生产物。

### 队列操作
- `RemoveOneFromQueue` `@ 0x4CA620`：线性查找 + 左移删除
- `CountTotal` `@ 0x4CA670`：生产中 + 队列中计数
- `IsQueued` `@ 0x4CA6B0`：仅队列中（不含生产中）

## 待确认

- `DemandProduction` 第三参数与 YRpp `shouldQueue` 的对应关系
- `Unsuspend` 资金不足时调用方是否重新挂起（原版行为按原样转写）
